/* LoongArch LP64S/LP64D typed physical frame selection smoke/negative probes.
 * The production selector is deliberately not registered while remaining
 * Core/ABI/unaligned-memory families are still being migrated. */
#include "target/loongarch64/machine_select.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool selected_case(const char *name,
    const MinicMachineLP64FrameContract *frame,
    const char *expect1, const char *expect2, const char *expect3,
    bool negative) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    size_t block;
    FILE *stream = NULL;
    char buffer[32768];
    bool ok = false;
    const char *stage = "initialize";
    size_t n;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_function_initialize(&machine, &target, name,
            MINIC_LOONG_MACHINE_FEATURE_BASE | MINIC_LOONG_MACHINE_FEATURE_FP))
        return false;
    machine.frame_size = frame->frame_size;
    machine.frame_alignment = frame->frame_alignment;
    machine.has_dynamic_stack = frame->has_dynamic_stack;
    if (!minic_machine_builder_initialize(&builder, &machine) ||
        !minic_machine_builder_create_block(&builder, NULL, false,
            false, &block))
        goto done;
    stage = "prologue";
    if (!minic_loongarch64_machine_select_prologue(&builder, frame)) goto done;
    stage = "epilogue";
    if (!minic_loongarch64_machine_select_epilogue(&builder, frame)) goto done;
    if (negative) {
        /* Forge an invalid target opcode/operand contract at an otherwise
         * valid machine position; only verifier may reject this mismatch. */
        machine.blocks[block].instructions[0].opcode = UINT32_MAX;
        ok = !minic_machine_gnu_asm_preflight(&machine, &policy);
        goto done;
    }
    stage = "preflight";
    if (!minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serializer";
    stream = tmpfile();
    if (stream == NULL ||
        !minic_machine_gnu_asm_emit_body(stream, &machine, &policy) ||
        fflush(stream) != 0 || fseek(stream, 0L, SEEK_SET) != 0)
        goto done;
    n = fread(buffer, 1U, sizeof(buffer) - 1U, stream);
    buffer[n] = '\0';
    ok = strstr(buffer, expect1) != NULL &&
         strstr(buffer, expect2) != NULL &&
         strstr(buffer, expect3) != NULL;
    if (!ok) fprintf(stderr, "Loong MIR %s output:\n%s\n", name, buffer);
done:
    if (!ok) fprintf(stderr, "Loong MIR selected frame failed: %s stage=%s ins=%zu\n",
        name, stage, machine.block_count != 0U ?
            machine.blocks[0].instruction_count : 0U);
    if (stream != NULL) fclose(stream);
    minic_machine_function_destroy(&machine);
    return ok;
}

static bool label_address_case(bool forged) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineOperand address_ops[2] = {{0}};
    MinicMachineOperand symbol_ops[2] = {{0}};
    MinicMachineOperand branch_op = {0};
    size_t entry, indirect;
    FILE *stream = NULL;
    char buffer[2048];
    size_t n;
    bool ok = false;
    const char *stage = "builder";
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_function_initialize(&machine, &target,
            "label_address", MINIC_LOONG_MACHINE_FEATURE_BASE |
                MINIC_LOONG_MACHINE_FEATURE_FP)) return false;
    machine.frame_alignment = 16U;
    if (!minic_machine_builder_initialize(&builder, &machine) ||
        !minic_machine_builder_create_block(&builder, NULL,
            false, false, &entry) ||
        !minic_machine_builder_create_block(&builder, NULL,
            !forged, false, &indirect) ||
        !minic_machine_builder_position_at(&builder, entry)) goto done;
    address_ops[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    address_ops[0].value.physical.reg = (MinicMachineRegister){12U, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    address_ops[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    address_ops[1].kind = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
    address_ops[1].value.block_id = indirect;
    symbol_ops[0] = address_ops[0];
    symbol_ops[1].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    symbol_ops[1].value.symbol.name = "internal_local";
    symbol_ops[1].value.symbol.relocation = MINIC_LOONG_RELOC_DIRECT;
    branch_op.kind = MINIC_MACHINE_OPERAND_BLOCK;
    branch_op.value.block_id = indirect;
    if (!minic_machine_builder_emit_selected(&builder,
            MINIC_LOONG_MACHINE_LA_LOCAL, 0U,
            symbol_ops, 2U, NULL, 0U) ||
        !minic_machine_builder_emit_selected(&builder,
            MINIC_LOONG_MACHINE_LA_BLOCK_ADDRESS, 0U,
            address_ops, 2U, NULL, 0U) ||
        !minic_machine_builder_emit_selected(&builder,
            MINIC_LOONG_MACHINE_B, MINIC_MACHINE_EFFECT_BRANCH,
            &branch_op, 1U, NULL, 0U) ||
        !minic_machine_builder_add_successor(&builder, indirect) ||
        !minic_machine_builder_position_at(&builder, indirect)) goto done;
    {
        const MinicMachineRegisterEffect read_ra = {
            {1U, 64U, MINIC_MACHINE_REGISTER_GENERAL},
            MINIC_MACHINE_REGISTER_READ
        };
        if (!minic_machine_builder_emit_selected(&builder,
                MINIC_LOONG_MACHINE_RET, MINIC_MACHINE_EFFECT_RETURN,
                NULL, 0U, &read_ra, 1U)) goto done;
    }
    if (forged) {
        stage = "negative_preflight";
        ok = !minic_machine_gnu_asm_preflight(&machine, &policy);
        goto done;
    }
    stage = "preflight";
    if (!minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    stream = tmpfile();
    if (stream == NULL ||
        !minic_machine_gnu_asm_emit_body(stream, &machine, &policy) ||
        fflush(stream) != 0 || fseek(stream, 0L, SEEK_SET) != 0) goto done;
    n = fread(buffer, 1U, sizeof(buffer) - 1U, stream);
    buffer[n] = '\0';
    ok = strstr(buffer, "la.local $t0, internal_local") != NULL &&
         strstr(buffer, "la.local $t0, .Lminic_mir_") != NULL;
    if (!ok) fprintf(stderr, "label probe output:\n%s\n", buffer);
done:
    if (!ok) fprintf(stderr, "Loong local machine address probe failed forged=%d stage=%s\n",
        forged ? 1 : 0, stage);
    if (stream != NULL) fclose(stream);
    minic_machine_function_destroy(&machine);
    return ok;
}

int main(void) {
    MinicMachineLP64FrameContract frame = {0};
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    assert(selected_case("leaf", &frame, "ret", "ret", "ret", false));
    frame.frame_size = 64U;
    frame.save_offset = 24U;
    frame.has_calls = true;
    assert(selected_case("save_ra", &frame,
        "addi.d $sp, $sp, -64", "st.d $ra, $sp, 24",
        "ld.d $ra, $sp, 24", false));
    frame.frame_size = 8192U;
    frame.has_dynamic_stack = true;
    frame.save_offset = 2048U;
    assert(selected_case("alloca", &frame,
        "li.d $t3, 8192", "stptr.d $s0, $sp, 2056",
        "move $sp, $s0", false));
    frame.frame_size = 16384U;
    frame.frame_alignment = 64U;
    frame.save_offset = 4080U;
    frame.saved_entry_sp_offset = 4096U;
    frame.has_indirect_return = true;
    frame.indirect_return_offset = 1024U;
    frame.variadic_count = 1U;
    frame.variadic_save_base = 256U;
    assert(selected_case("realign_varargs", &frame,
        "addi.d $s0, $sp, 63", "stptr.d $t0, $s0, 4096",
        "st.d $a7, $s0, 312", false));
    assert(selected_case("forged_opcode", &frame, "", "", "", true));
    assert(label_address_case(false));
    assert(label_address_case(true));
    puts("LoongArch64 typed MachineFrame positive/negative PASS");
    return 0;
}
