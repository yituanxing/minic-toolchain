/* Loong Linux descriptor registry: source-ordered typed ID, independently
 * checked GNU template, zero-only variant, and fail-closed forged metadata.
 * These IDs deliberately follow the single source/machine descriptor table. */
#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/asm_contract_source.h"
#include "target/loongarch64/machine_source_asm.h"
#include "frontend/ast.h"

/* This is an assertion-based machine-contract test: release -DNDEBUG must
 * not silently disable its forged-Core and no-partial-output checks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool gate(uint32_t id, uint32_t variant, const char *literal) {
    return minic_loongarch64_typed_descriptor_matches_template(
        id, variant, literal, literal ? strlen(literal) : 0U);
}

static bool scalar_gate(uint32_t id, uint32_t variant, const char *literal) {
    return minic_loongarch64_typed_scalar_form_matches_template(
        id, variant, literal, literal ? strlen(literal) : 0U);
}

/* The source qualifier admits exact C ranks and GNU operand roles; only
 * then may numeric template normalization publish the same machine form.
 * Exercise the real Source -> Core contract, not just the emitter matcher. */
static void descriptor_source_normalized_seal(void) {
    static const char machine[] = "cpucfg %0, %1";
    static const char altered[] = "cpucfg %1, %0";
    MinicExpression expressions[2] = {{0}};
    MinicC0Program program = {0};
    MinicInlineAsmOperand output = {0}, input = {0};
    MinicInlineAsm source = {0};
    MinicTargetInlineAsmContract admitted = {0}, forged;
    MinicSourceBackend backend = {0};
    MinicTargetInvocation invocation;
    assert(minic_target_invocation_begin(
        &invocation, minic_target_info_for_architecture(MINIC_TARGET_ARCH_LOONGARCH64)));
    expressions[0].type = minic_type_unsigned_int();
    expressions[0].value_category = MINIC_VALUE_LVALUE;
    expressions[1].type = minic_type_int();
    program.expressions = expressions;
    program.expression_count = 2U;
    output.constraint_text = "=&r";
    output.constraint_length = 3U;
    output.expression = 0U;
    output.access = MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    input.constraint_text = "r";
    input.constraint_length = 1U;
    input.expression = 1U;
    source.template_text = (char *)machine;
    source.template_length = sizeof(machine) - 1U;
    source.is_volatile = true;
    source.outputs = &output;
    source.output_count = 1U;
    source.inputs = &input;
    source.input_count = 1U;
    backend.seal_normalized_inline_asm = minic_loongarch64_seal_normalized_inline_asm;
    assert(minic_loongarch64_qualify_inline_asm(
        &invocation, &program, &source, &admitted));
    assert(minic_source_backend_capture_asm_operand_source(
        &source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &admitted));
    assert(admitted.target_machine_form_id == 5U);
    assert(minic_source_backend_seal_normalized_inline_asm(
        &backend, &invocation, &program, &source, machine, sizeof(machine) - 1U, &admitted));
    assert(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &invocation, &program, &source, altered, sizeof(altered) - 1U, &admitted));
    forged = admitted;
    forged.target_machine_form_id = 1U;
    assert(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &invocation, &program, &source, machine, sizeof(machine) - 1U, &forged));
    forged = admitted;
    forged.kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    assert(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &invocation, &program, &source, machine, sizeof(machine) - 1U, &forged));
    forged = admitted;
    forged.output_count = 0U;
    assert(!minic_source_backend_seal_normalized_inline_asm(
        &backend, &invocation, &program, &source, machine, sizeof(machine) - 1U, &forged));
    expressions[0].type = minic_type_unsigned_long_long();
    assert(!minic_loongarch64_qualify_inline_asm(
        &invocation, &program, &source, &forged));
}

/* MachineIR itself, rather than the removed FILE* Core emitter, is the
 * physical execution authority. Positive CPUCFG source bindings must
 * survive the sealed template's validation and actual GNU serialization.
 * A forged form/operand must fail before even one byte is written. */
static void descriptor_machine_source_positive(void) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_LOONGARCH64);
    MinicMachineFunction function = {0};
    MinicMachineInstruction ins = {0};
    MinicMachineOperand operands[2] = {{0}};
    MinicMachineGNUAsmLabels labels = {0};
    char bytes[1024] = {0};
    FILE *out = tmpfile();
    size_t length;
    assert(target && out);
    function.target = target;
    operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    operands[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    operands[0].value.physical.reg.number = 12U;
    operands[0].value.physical.reg.width_bits = 64U;
    operands[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    operands[0].asm_constraint = "=&r";
    operands[1].kind = MINIC_MACHINE_OPERAND_REGISTER;
    operands[1].value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    operands[1].value.physical.reg.number = 13U;
    operands[1].value.physical.reg.width_bits = 64U;
    operands[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    operands[1].asm_constraint = "r";
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE;
    ins.source_asm_template = "cpucfg %0, %1";
    ins.source_asm_length = strlen(ins.source_asm_template);
    ins.source_asm_machine_form_id = 5U;
    ins.operand_count = 2U;
    ins.operands = operands;
    assert(minic_loongarch64_machine_validate_source_asm(target, &ins, NULL));
    assert(minic_loongarch64_machine_emit_source_asm(
        out, &function, &ins, &labels, NULL));
    assert(fflush(out) == 0 && fseek(out, 0L, SEEK_SET) == 0);
    length = fread(bytes, 1U, sizeof(bytes) - 1U, out);
    assert(strstr(bytes, "cpucfg $t0, $t1") != NULL && length > 0U);
    assert(fclose(out) == 0);
    ins.source_asm_machine_form_id = 1U;
    assert(!minic_loongarch64_machine_validate_source_asm(target, &ins, NULL));
    ins.source_asm_machine_form_id = 5U;
    operands[0].value.physical.reg.number = 0U;
    assert(!minic_loongarch64_machine_validate_source_asm(target, &ins, NULL));
    operands[0].value.physical.reg.number = 12U;
    ins.source_asm_template = "cpucfg %1, %0";
    ins.source_asm_length = strlen(ins.source_asm_template);
    assert(!minic_loongarch64_machine_validate_source_asm(target, &ins, NULL));
    puts("LOONG_TYPED_DESCRIPTOR_MACHINE=PASS physical_cpucfg=1 forged_id=1 bad_physical=1 forged_template=1");
}

static void machine_reject(uint32_t id, uint32_t variant, const char *literal) {
    const MinicTargetInfo *target =
        minic_target_info_for_architecture(MINIC_TARGET_ARCH_LOONGARCH64);
    MinicMachineInstruction ins = {0};
    MinicMachineFunction function = {0};
    MinicMachineGNUAsmLabels labels = {0};
    FILE *out = tmpfile();
    assert(target && out);
    function.target = target;
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.source_asm_template = (char *)literal;
    ins.source_asm_length = strlen(literal);
    ins.source_asm_machine_form_id = id;
    ins.source_asm_machine_form_variant = variant;
    assert(!minic_loongarch64_machine_validate_source_asm(target, &ins, NULL));
    assert(!minic_loongarch64_machine_emit_source_asm(
        out, &function, &ins, &labels, NULL));
    assert(ftell(out) == 0L);
    assert(fclose(out) == 0);
}

#define CHECK_FORM(id, literal, corrupt) do { \
    assert(gate(id, 0U, literal)); \
    assert(minic_loongarch64_descriptor_template_requires_typed_id( \
        literal, strlen(literal))); \
    assert(!gate(id, 1U, literal)); \
    assert(!gate(id, 0U, corrupt)); \
    assert(!minic_loongarch64_descriptor_template_requires_typed_id( \
        corrupt, strlen(corrupt))); \
    assert(!gate(0U, 0U, literal)); \
    assert(!gate(15U, 0U, literal)); \
} while (0)

#define CHECK_SCALAR(id, variant, literal, corrupt) do { \
    assert(scalar_gate(id, variant, literal)); \
    assert(!scalar_gate(id, 0U, literal)); \
    assert(!scalar_gate(id, variant + 1U, literal)); \
    assert(!scalar_gate(id, variant, corrupt)); \
    assert(!scalar_gate(0U, variant, literal)); \
    machine_reject(0U, 0U, literal); \
    machine_reject(id, variant, corrupt); \
} while (0)

int main(void) {
    static const char irq_core[] =
        "move $s0,$sp\nmove $sp,%0\nmove $a0,%1\n"
        "bl handle_loongarch_irq\nmove $sp,$s0";
    static const char add_unless[] =
        "0:ll.w %0,%2\nbeq %0,%4,1f\nadd.w %1,%0,%3\n"
        "sc.w %1,%2\nbeqz %1,0b\nb 2f\n1:dbar 0x700\n2:";
    descriptor_source_normalized_seal();
    descriptor_machine_source_positive();
    CHECK_FORM(1U, "cacop %0, %1", "cacop %0, %1; break 0");
    CHECK_FORM(2U, "syscall 0", "syscall 0; break 0");
    CHECK_FORM(5U, "cpucfg %0, %1", "cpucfg %0, %1; break 0");
    CHECK_FORM(13U, "sltu %0,%1,%2\n\tsub.d %0,$zero,%0",
        "sltu %0,%1,%2\n\tsub.d %0,$zero,%0; break 0");
    CHECK_FORM(14U,
        "1: ll.d %1,%0\n\tbnez %1,2f\n\tor %1,%1,%2\n"
        "\tsc.d %1,%0\n\tbeqz %1,1b\n\tnop\n2: dbar 0x700",
        "1: ll.d %1,%0\n\tbnez %1,2f\n\tor %1,%1,%2\n"
        "\tsc.d %1,%0\n\tbeqz %1,1b\n\tnop\n2: dbar 0x700; break 0");
    assert(!gate(1U, 0U, "syscall 0"));
    assert(!gate(5U, 0U, "cacop %0, %1"));
    /* A forged descriptor ID must reject even unrelated SIMD-like text;
     * the typed path runs before the target's SIMD fallback. */
    assert(!gate(1U, 0U, "xvadd.b $xr0, $xr1, $xr2"));
    assert(!gate(2U, 0U, NULL));
    machine_reject(0U, 0U, "cacop %0, %1");
    machine_reject(0U, 0U, "syscall 0");
    machine_reject(1U, 0U, "syscall 0");
    machine_reject(1U, 1U, "cacop %0, %1");
    machine_reject(15U, 0U, "cacop %0, %1");
    machine_reject(1U, 0U, "xvadd.b $xr0, $xr1, $xr2");
    CHECK_SCALAR(MINIC_LOONG_MACHINE_ASM_CLOCK, 1U,
        "rdtime.d %0,%1", "rdtime.d %0,%1; nop");
    CHECK_SCALAR(MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH, 1U,
        irq_core, "move $s0,$sp\nmove $sp,%0\nmove $a0,%1\n"
                  "bl hijack_loongarch_irq\nmove $sp,$s0");
    CHECK_SCALAR(MINIC_LOONG_MACHINE_ASM_INVTLB, 1U,
        "invtlb %0,%1,%2", "invtlb %0,%1,%2; nop");
    CHECK_SCALAR(MINIC_LOONG_MACHINE_ASM_ADD_UNLESS, 1U,
        add_unless, "0:ll.w %0,%2\nbeq %0,%4,1f\nadd.w %1,%0,%3\n"
                    "sc.w %1,%2\nbeqz %1,0b\nb 2f\n1:dbar 0x700; nop\n2:");
    assert(!scalar_gate(MINIC_LOONG_MACHINE_ASM_FUTEX_RMW, 1U,
        "1:ll.w%1,%4; nop"));
    assert(!scalar_gate(MINIC_LOONG_MACHINE_ASM_CLOCK, 1U,
        "invtlb %0,%1,%2"));
    machine_reject(MINIC_LOONG_MACHINE_ASM_CLOCK, 1U, "invtlb %0,%1,%2");
    machine_reject(MINIC_LOONG_MACHINE_ASM_FUTEX_RMW, 1U, "1:ll.w%1,%4; nop");
    puts("LOONG_TYPED_DESCRIPTOR_GATE=PASS exact=5 zero_id_reserved=5 forged_id=5 forged_variant=5 corrupted_template=5 cross_id=3 null=1 machine_reject=6 scalar_exact=4 scalar_mutated=4 scalar_id0=4 scalar_machine_reject=10 partial_s=0");
    return 0;
}
