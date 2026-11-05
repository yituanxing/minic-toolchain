/* AAPCS64 typed frame-selection compatibility probes. These are real owned
 * MachineFunctions, independently verified and GNU-serialized. They do not
 * claim that the Core whole-function production selector is switched. */
#include "target/aarch64/machine_frame_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/data_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool frame_case(const char *name,
    const MinicMachineLP64FrameContract *frame, const char *must_have,
    const char *also_have, const char *third) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    size_t block;
    FILE *stream;
    char buffer[16384];
    size_t size;
    bool ok = false;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    if (!minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_function_initialize(&machine, &target, name,
            MINIC_A64_MACHINE_FEATURE_BASE | MINIC_A64_MACHINE_FEATURE_FP_SIMD))
        return false;
    machine.frame_size = frame->frame_size;
    machine.frame_alignment = frame->frame_alignment;
    machine.has_dynamic_stack = frame->has_dynamic_stack;
#define STEP(call) do { if (!(call)) { \
    fprintf(stderr, "%s failed at %s\n", name, #call); goto done; \
} } while (0)
    STEP(minic_machine_builder_initialize(&builder, &machine));
    STEP(minic_machine_builder_create_block(&builder, NULL, false,
        false, &block));
    STEP(minic_machine_builder_position_at(&builder, block));
    if (!minic_a64_machine_emit_prologue(&builder, frame)) {
        fprintf(stderr, "%s prologue partial count=%zu\n", name,
            machine.blocks[block].instruction_count);
        goto done;
    }
    STEP(minic_a64_machine_emit_epilogue(&builder, frame));
    STEP(minic_machine_gnu_asm_preflight(&machine, &policy));
#undef STEP
    stream = tmpfile();
    if (stream == NULL) goto done;
    if (!minic_machine_gnu_asm_emit_body(stream, &machine, &policy) ||
        fflush(stream) != 0 || fseek(stream, 0L, SEEK_SET) != 0) {
        fclose(stream);
        goto done;
    }
    size = fread(buffer, 1U, sizeof(buffer) - 1U, stream);
    buffer[size] = '\0';
    fclose(stream);
    if (strstr(buffer, must_have) == NULL ||
        strstr(buffer, also_have) == NULL ||
        strstr(buffer, third) == NULL) {
        fprintf(stderr, "%s serialized unexpectedly:\n%s\n", name, buffer);
        goto done;
    }
    ok = true;
done:
    if (!ok) fprintf(stderr, "A64 MIR frame case failed: %s\n", name);
    minic_machine_function_destroy(&machine);
    return ok;
}

static bool incoming_case(void) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    const MinicA64ScratchPolicy spare = {0U, 0U};
    const MinicA64FrameView view = {true, 65536U, 0U, 32768U};
    MinicMachineLP64FrameContract physical = {0};
    bool ok = false;
    size_t block;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    if (!minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_function_initialize(&machine, &target, "incoming",
            MINIC_A64_MACHINE_FEATURE_BASE | MINIC_A64_MACHINE_FEATURE_FP_SIMD))
        return false;
    machine.has_dynamic_stack = true;
    machine.frame_size = 65536U;
    machine.frame_alignment = 32U;
    physical.frame_size = 65536U;
    physical.frame_alignment = 32U;
    physical.has_dynamic_stack = true;
    physical.saved_entry_sp_offset = 32768U;
    physical.save_offset = 32752U;
    if (!minic_machine_builder_initialize(&builder, &machine) ||
        !minic_machine_builder_create_block(&builder, NULL, false,
            false, &block) ||
        !minic_machine_builder_position_at(&builder, block) ||
        !minic_a64_machine_emit_prologue(&builder, &physical) ||
        !minic_a64_machine_emit_frame_load(&builder, &view,
            (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, 5000U},
            (MinicA64FrameRegister){MINIC_A64_REG_X, 9U}, spare) ||
        !minic_a64_machine_emit_frame_store(&builder, &view,
            (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
            (MinicA64FrameRegister){MINIC_A64_REG_X, 17U}, spare) ||
        !minic_a64_machine_emit_frame_address(&builder, &view,
            (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, 5000U},
            11U, spare)) goto done;
    /* Append a complete, independently verifiable return terminator. */
    if (!minic_a64_machine_emit_epilogue(&builder, &physical)) goto done;
    ok = minic_machine_gnu_asm_preflight(&machine, &policy);
done:
    if (!ok) fprintf(stderr, "A64 MIR large incoming frame failed\n");
    minic_machine_function_destroy(&machine);
    return ok;
}

static MinicMachineOperand gp(unsigned int reg, unsigned int width) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, (uint16_t)width,
        MINIC_MACHINE_REGISTER_GENERAL};
    return op;
}

static MinicMachineOperand integer(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}

static MinicMachineOperand pair_offset(unsigned int base, int64_t offset) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.target_mode = MINIC_A64_ADDRESS_PAIR_OFFSET;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){base, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.displacement = offset;
    return op;
}

static bool selected(MinicMachineBuilder *builder,
    const MinicMachineGNUAsmPolicy *policy, MinicA64MachineOpcode opcode,
    MinicMachineOperand *ops, size_t count) {
    MinicMachineOpcodeContract contract = {0};
    size_t i;
    if (!policy->verify.lookup_opcode(builder->function->target,
        opcode, &contract, policy->verify.context) ||
        contract.operand_count != count) return false;
    for (i = 0U; i < count; ++i) {
        if (ops[i].kind != contract.operand_kinds[i]) return false;
        if (ops[i].kind == MINIC_MACHINE_OPERAND_REGISTER)
            ops[i].value.physical.access = contract.register_access[i];
    }
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, ops, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool opcode_forms_case(void) {
    MinicTargetInfo target = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicMachineOperand ops[4] = {0};
    size_t block;
    FILE *stream;
    char buffer[8192];
    size_t n;
    bool ok = false;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = minic_aarch64_linux_data_layout();
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    if (!minic_aarch64_machine_gnu_asm_policy_initialize(&target, &policy) ||
        !minic_machine_function_initialize(&machine, &target, "forms",
            MINIC_A64_MACHINE_FEATURE_BASE | MINIC_A64_MACHINE_FEATURE_FP_SIMD))
        return false;
    machine.frame_alignment = frame.frame_alignment;
    if (!minic_machine_builder_initialize(&builder, &machine) ||
        !minic_machine_builder_create_block(&builder, NULL, false,
            false, &block) ||
        !minic_a64_machine_emit_prologue(&builder, &frame)) goto done;
#define EMIT3(op) if (!selected(&builder, &policy, (op), ops, 3U)) goto done
    ops[0] = gp(10U, 64U); ops[1] = gp(9U, 64U); ops[2] = integer(15);
    EMIT3(MINIC_A64_MACHINE_ADDS_IMM);
    ops[0] = gp(12U, 64U); ops[1] = gp(12U, 64U); ops[2] = integer(1);
    EMIT3(MINIC_A64_MACHINE_SUBS_IMM);
    ops[0] = gp(9U, 64U); ops[1] = gp(9U, 64U);
    ops[2] = gp(10U, 64U); ops[3] = integer(8);
    if (!selected(&builder, &policy, MINIC_A64_MACHINE_ORR_LSL,
        ops, 4U)) goto done;
    ops[0] = gp(9U, 32U); ops[1] = gp(10U, 64U); ops[2] = integer(1);
    EMIT3(MINIC_A64_MACHINE_LDRB_POST);
    ops[0] = gp(9U, 32U); ops[1] = gp(11U, 64U); ops[2] = integer(1);
    EMIT3(MINIC_A64_MACHINE_STRB_POST);
    ops[0] = gp(11U, 64U); ops[1] = gp(12U, 64U);
    ops[2] = pair_offset(10U, 16);
    EMIT3(MINIC_A64_MACHINE_STP_OFFSET_X);
    ops[2] = pair_offset(10U, -16);
    EMIT3(MINIC_A64_MACHINE_LDP_OFFSET_X);
    ops[0] = gp(10U, 64U); ops[1] = gp(10U, 64U);
    ops[2] = integer(-16);
    EMIT3(MINIC_A64_MACHINE_AND_ALIGN_IMM);
#undef EMIT3
    if (!minic_a64_machine_emit_epilogue(&builder, &frame) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stream = tmpfile();
    if (stream == NULL) goto done;
    if (!minic_machine_gnu_asm_emit_body(stream, &machine, &policy) ||
        fflush(stream) != 0 || fseek(stream, 0L, SEEK_SET) != 0) {
        fclose(stream);
        goto done;
    }
    n = fread(buffer, 1U, sizeof(buffer) - 1U, stream);
    buffer[n] = '\0';
    fclose(stream);
    ok = strstr(buffer, "adds x10, x9, #15") != NULL &&
        strstr(buffer, "subs x12, x12, #1") != NULL &&
        strstr(buffer, "orr x9, x9, x10, lsl #8") != NULL &&
        strstr(buffer, "ldrb w9, [x10], #1") != NULL &&
        strstr(buffer, "strb w9, [x11], #1") != NULL &&
        strstr(buffer, "stp x11, x12, [x10, #16]") != NULL &&
        strstr(buffer, "ldp x11, x12, [x10, #-16]") != NULL &&
        strstr(buffer, "and x10, x10, #-16") != NULL;
    if (!ok) fprintf(stderr, "A64 opcode family emitted:\n%s\n", buffer);
done:
    if (!ok) fprintf(stderr, "A64 opcode forms synthetic case failed\n");
    minic_machine_function_destroy(&machine);
    return ok;
}

int main(void) {
    MinicMachineLP64FrameContract frame = {0};
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    assert(frame_case("leaf", &frame, "stp x29, x30, [sp, #-16]!",
        "mov x29, sp", "ldp x29, x30, [sp], #16"));
    frame.frame_size = 256U;
    frame.has_calls = true;
    frame.save_offset = 16U;
    assert(frame_case("call", &frame, "sub sp, sp, x12",
        "str x30, [sp, #16]", "add sp, sp, x12"));
    frame.frame_size = 8192U;
    frame.has_dynamic_stack = true;
    frame.save_offset = 2048U;
    assert(frame_case("alloca", &frame, "str x19, [sp, #2056]",
        "mov sp, x19", "ldr x19, [x19, #2056]"));
    frame.frame_size = 16384U;
    frame.frame_alignment = 64U;
    frame.saved_entry_sp_offset = 4096U;
    frame.save_offset = 4080U;
    frame.variadic_count = 1U;
    frame.variadic_save_base = 256U;
    frame.has_indirect_return = true;
    frame.indirect_return_offset = 1024U;
    assert(frame_case("realign_varargs", &frame,
        "and x9, x9, #-64", "str q0, [x19, #352]",
        "mov sp, x9"));
    assert(incoming_case());
    assert(opcode_forms_case());
    puts("A64 physical MachineFrame synthetic contracts PASS");
    return 0;
}
