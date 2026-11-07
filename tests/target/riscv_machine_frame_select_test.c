/* Physical RV32I/RV64I prologue, frame-reference and exit selection.
 * This intentionally tests native MachineFunction/CFG/ABI, not the old Core
 * FILE* emitter. Frame plans are synthetic finalized ABI facts. */
#include "target/riscv/machine_frame_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_target_state.h"
#include "target/data_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "RV physical MIR:%d: %s\n", __LINE__, #expr); \
    return false; } } while (0)

static bool setup(MinicTargetInfo *target, MinicDataLayout *layout,
                  unsigned xlen) {
    MinicRiscvIsa isa = {0};
    memset(target, 0, sizeof(*target));
    memset(layout, 0, sizeof(*layout));
    layout->byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout->pointer_size = xlen / 8U;
    layout->pointer_alignment = xlen / 8U;
    target->architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target->data_layout = layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    return minic_riscv_target_isa_store(target, &isa);
}

static bool body_contains(const MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy, const char *a, const char *b) {
    FILE *stream = tmpfile();
    char result[16384];
    size_t bytes;
    CHECK(stream != NULL);
    CHECK(minic_machine_gnu_asm_emit_body(stream, function, policy));
    CHECK(fflush(stream) == 0);
    CHECK(fseek(stream, 0, SEEK_SET) == 0);
    bytes = fread(result, 1U, sizeof(result) - 1U, stream);
    result[bytes] = '\0';
    CHECK(fclose(stream) == 0);
    CHECK(strstr(result, a) != NULL);
    CHECK(strstr(result, b) != NULL);
    return true;
}

static bool run_frame(unsigned xlen, bool dynamic, bool realign,
                      bool variadic, bool large, bool saved) {
    MinicTargetInfo target;
    MinicDataLayout layout;
    MinicMachineFunction fn = {0};
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    MinicRiscvPhysicalCalleeSave saved_s1 = {9U, 40U};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFrameObject object = {0};
    size_t prologue, core, epilogue, object_id;
    const unsigned word = xlen / 8U;
    CHECK(setup(&target, &layout, xlen));
    CHECK(minic_riscv_machine_gnu_asm_policy(&target, &policy));
    CHECK(minic_machine_function_initialize(&fn, &target,
        xlen == 32U ? "rv32_native_frame" : "rv64_native_frame", 0U));
    frame.frame_size = large ? 8192U : 128U;
    frame.stack_alignment = realign ? 64U : 16U;
    frame.outgoing_size = 0U;
    frame.return_address_offset = 0U;
    frame.saved_frame_base_offset = 8U;
    frame.hidden_result_pointer_offset = 16U;
    frame.entry_sp_offset = 64U;
    frame.dynamic_call_area_size = dynamic && xlen == 64U ? 32U : 0U;
    frame.variadic = variadic;
    frame.first_variadic_gpr = 3U;
    frame.varargs_size = variadic ? 5U * word : 0U;
    frame.varargs_offset = frame.frame_size - frame.varargs_size;
    frame.has_dynamic_stack = dynamic;
    frame.has_dynamic_stack_alignment = realign;
    frame.saves_return_address = true;
    frame.has_hidden_result_pointer = large;
    frame.callee_saved_count = saved ? 1U : 0U;
    frame.callee_saved = saved ? &saved_s1 : NULL;
    fn.frame_size = frame.frame_size;
    fn.frame_alignment = 16U;
    fn.has_dynamic_stack = dynamic;
    CHECK(minic_machine_builder_initialize(&builder, &fn));
    CHECK(minic_machine_builder_create_block(&builder, NULL, false,
        true, &prologue) && prologue == 0U);
    CHECK(minic_machine_builder_create_block(&builder, NULL, false,
        false, &core) && core == 1U);
    CHECK(minic_machine_builder_create_block(&builder, NULL, false,
        true, &epilogue) && epilogue == 2U);
    object.size_bytes = word;
    object.alignment_bytes = word;
    object.area = MINIC_MACHINE_FRAME_LOCAL;
    object.base = dynamic ? MINIC_MACHINE_FRAME_BASE_FRAME_POINTER :
                            MINIC_MACHINE_FRAME_BASE_STACK_POINTER;
    object.has_fixed_offset = true;
    object.fixed_offset = 96;
    CHECK(minic_machine_function_add_frame_object(&fn, &object, &object_id));
    CHECK(minic_riscv_machine_select_prologue(&builder, &frame, core));
    CHECK(minic_machine_builder_position_at(&builder, core));
    CHECK(minic_riscv_machine_select_frame_object(&builder, object_id, 0U,
        5U, false, word));
    CHECK(minic_riscv_machine_select_frame_object(&builder, object_id, 0U,
        6U, true, word));
    CHECK(minic_machine_builder_add_successor(&builder, epilogue));
    CHECK(minic_machine_builder_set_fallthrough(&builder, epilogue));
    if (large) {
        MinicRiscvPhysicalFrame invalid = frame;
        invalid.hidden_result_pointer_offset = frame.return_address_offset;
        CHECK(!minic_riscv_machine_select_epilogue(
            &builder, &invalid, epilogue));
    }
    CHECK(minic_riscv_machine_select_epilogue(&builder, &frame, epilogue));
    CHECK(minic_machine_gnu_asm_preflight(&fn, &policy));
    CHECK(body_contains(&fn, &policy, xlen == 32U ? "sw x1" : "sd x1",
        "  ret\n"));
    /* Deliberately forged oversized frame ref must fail closed. */
    fn.frame_objects[object_id].fixed_offset = 4096;
    CHECK(!minic_machine_gnu_asm_preflight(&fn, &policy));
    minic_machine_function_destroy(&fn);
    return true;
}

static bool run_backedge(unsigned xlen) {
    MinicTargetInfo target;
    MinicDataLayout layout;
    MinicMachineFunction fn = {0};
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineOperand la[2] = {{0}};
    MinicMachineOperand jump[2] = {{0}};
    size_t prologue, core, exit;
    CHECK(setup(&target, &layout, xlen));
    CHECK(minic_riscv_machine_gnu_asm_policy(&target, &policy));
    CHECK(minic_machine_function_initialize(&fn, &target,
        xlen == 32U ? "rv32_core_backedge" : "rv64_core_backedge", 0U));
    fn.frame_size = 128U;
    fn.frame_alignment = 16U;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = true;
    CHECK(minic_machine_builder_initialize(&builder, &fn));
    CHECK(minic_machine_builder_create_block(&builder, NULL, false,
        true, &prologue) && prologue == 0U);
    CHECK(minic_machine_builder_create_block(&builder, NULL, true,
        false, &core) && core == 1U);
    CHECK(minic_machine_builder_create_block(&builder, NULL, false,
        true, &exit) && exit == 2U);
    CHECK(minic_riscv_machine_select_prologue(&builder, &frame, core));
    CHECK(minic_machine_builder_position_at(&builder, core));
    la[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    la[0].value.physical.reg = (MinicMachineRegister){31U,
        (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    la[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    la[1].kind = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
    la[1].value.block_id = core;
    CHECK(minic_machine_builder_emit_selected(&builder,
        MINIC_RISCV_MACHINE_LA_BLOCK, 0U, la, 2U, NULL, 0U));
    jump[0] = la[0];
    jump[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    jump[1].kind = MINIC_MACHINE_OPERAND_BLOCK;
    jump[1].value.block_id = core;
    CHECK(minic_machine_builder_emit_selected(&builder,
        MINIC_RISCV_MACHINE_JALR_BLOCK, MINIC_MACHINE_EFFECT_BRANCH,
        jump, 2U, NULL, 0U));
    CHECK(minic_machine_builder_add_successor(&builder, core));
    CHECK(minic_riscv_machine_select_epilogue(&builder, &frame, exit));
    CHECK(minic_machine_gnu_asm_preflight(&fn, &policy));
    CHECK(body_contains(&fn, &policy, xlen == 32U ?
        ".Lminic_mir_rv32_core_backedge_1:" :
        ".Lminic_mir_rv64_core_backedge_1:", "jalr zero, 0(x31)"));
    minic_machine_function_destroy(&fn);
    return true;
}

int main(void) {
    const unsigned widths[] = {32U, 64U};
    size_t i;
    for (i = 0U; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        if (!run_frame(widths[i], false, false, false, false, false) ||
            !run_frame(widths[i], true, false, true, false, true) ||
            !run_frame(widths[i], true, true, true, true, true) ||
            !run_backedge(widths[i])) return 1;
    }
    puts("RV32I/RV64I typed physical MachineFrame+CFG PASS");
    return 0;
}
