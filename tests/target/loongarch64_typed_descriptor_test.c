/* Loong Linux descriptor registry: source-ordered typed ID, independently
 * checked GNU template, zero-only variant, and fail-closed forged metadata.
 * These IDs deliberately follow the single source/machine descriptor table. */
#include "target/loongarch64/asm_contract.h"
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
    MinicBackend backend = {0};
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
    assert(minic_loongarch64_qualify_inline_asm(&program, &source, &admitted));
    assert(minic_backend_capture_asm_operand_source(
        &source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, &admitted));
    assert(admitted.target_machine_form_id == 5U);
    assert(minic_backend_seal_normalized_inline_asm(
        &backend, &program, &source, machine, sizeof(machine) - 1U, &admitted));
    assert(!minic_backend_seal_normalized_inline_asm(
        &backend, &program, &source, altered, sizeof(altered) - 1U, &admitted));
    forged = admitted;
    forged.target_machine_form_id = 1U;
    assert(!minic_backend_seal_normalized_inline_asm(
        &backend, &program, &source, machine, sizeof(machine) - 1U, &forged));
    forged = admitted;
    forged.kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    assert(!minic_backend_seal_normalized_inline_asm(
        &backend, &program, &source, machine, sizeof(machine) - 1U, &forged));
    forged = admitted;
    forged.output_count = 0U;
    assert(!minic_backend_seal_normalized_inline_asm(
        &backend, &program, &source, machine, sizeof(machine) - 1U, &forged));
    expressions[0].type = minic_type_unsigned_long_long();
    assert(!minic_loongarch64_qualify_inline_asm(&program, &source, &forged));
}

/* The physical emitter consumes sealed Core execution types; no C nominal
 * rank can be recovered from the C-free CoreValue schema. Positive emission
 * fixtures catch an ownership regression that rejection tests alone miss. */
static bool cpucfg_emit_matches(const MinicDirectLP64BlockContext *ctx,
                                const MinicCoreInstruction *ins,
                                bool accepted) {
    char bytes[1024] = {0};
    size_t length;
    bool emitted;
    FILE *out = tmpfile();
    assert(out != NULL);
    emitted = minic_loongarch64_emit_qualified_structured_asm(out, ctx, ins);
    assert(emitted == accepted);
    assert(fflush(out) == 0);
    assert(fseek(out, 0L, SEEK_SET) == 0);
    length = fread(bytes, 1U, sizeof(bytes) - 1U, out);
    assert(fclose(out) == 0);
    if (!accepted) return length == 0U;
    return strstr(bytes, "cpucfg $t0, $t1") != NULL &&
        strstr(bytes, "st.w") != NULL;
}

static void descriptor_core_type_machine_positive(void) {
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, u32_id, s32_id, ptr_u32_id, ptr_u64_id, u64_id;
    MinicCoreValue values[2] = {0};
    MinicCoreFunction fn = {0};
    MinicCoreInlineAsm descriptor = {0};
    MinicCoreStructuredInlineAsmOperand operands[2] = {{0}};
    MinicCoreInstruction ins = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    size_t offsets[2] = {16U, 24U};
    minic_core_type_context_initialize(&types);
    type.kind = MINIC_CORE_TYPE_VOID;
    assert(minic_core_type_register(&types, &type, &void_id));
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    assert(minic_core_type_register(&types, &type, &u32_id));
    type.value.integer.is_signed = true;
    assert(minic_core_type_register(&types, &type, &s32_id));
    type.value.integer.is_signed = false;
    type.value.integer.bit_width = 64U;
    type.size = 8U;
    type.alignment = 8U;
    assert(minic_core_type_register(&types, &type, &u64_id));
    memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = u32_id;
    assert(minic_core_type_register(&types, &type, &ptr_u32_id));
    type.value.pointer.pointee = u64_id;
    assert(minic_core_type_register(&types, &type, &ptr_u64_id));
    values[0].core_type_id = ptr_u32_id;
    values[1].core_type_id = s32_id;
    /* Machine operands have CoreType identities, not source C descriptors. */
    descriptor.template_text = "cpucfg %0, %1";
    descriptor.template_length = strlen(descriptor.template_text);
    descriptor.is_volatile = true;
    descriptor.target_machine_form_id = 5U;
    operands[0].operand_index = 0U;
    operands[0].value = 0U;
    operands[0].kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    operands[1].operand_index = 1U;
    operands[1].value = 1U;
    operands[1].kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.core_type_id = void_id;
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    ins.value.structured_inline_asm.operand_count = 2U;
    ins.value.structured_inline_asm.operands[0] = operands[0];
    ins.value.structured_inline_asm.operands[1] = operands[1];
    fn.type_context = &types;
    fn.values = values;
    fn.value_count = 2U;
    fn.inline_asms = &descriptor;
    fn.inline_asm_count = 1U;
    plan.value_count = 2U;
    plan.value_offsets = offsets;
    ctx.fn = &fn;
    ctx.frame_plan = &plan;
    assert(cpucfg_emit_matches(&ctx, &ins, true));
    values[0].core_type_id = u32_id;
    assert(cpucfg_emit_matches(&ctx, &ins, false));
    values[0].core_type_id = u64_id;
    assert(cpucfg_emit_matches(&ctx, &ins, false));
    values[0].core_type_id = ptr_u64_id;
    assert(cpucfg_emit_matches(&ctx, &ins, false));
    values[0].core_type_id = ptr_u32_id;
    /* Invalid contexts must fail without issuing even one instruction. */
    fn.type_context = NULL;
    assert(cpucfg_emit_matches(&ctx, &ins, false));
    fn.type_context = &types;
    ins.value.structured_inline_asm.operands[0].value = 3U;
    assert(cpucfg_emit_matches(&ctx, &ins, false));
    minic_core_type_context_destroy(&types);
    puts("LOONG_TYPED_DESCRIPTOR_CORETYPE=PASS machine_positive=1 poisoned_C=2 wrong_core=3 null_context=1 bad_ssa=1 partial_s=0");
}

static void machine_reject(uint32_t id, uint32_t variant, const char *literal) {
    char text[256];
    MinicCoreInlineAsm descriptor = {0};
    MinicCoreFunction function = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicCoreInstruction ins = {0};
    FILE *out = tmpfile();
    assert(out != NULL);
    assert(strlen(literal) + 1U <= sizeof(text));
    memcpy(text, literal, strlen(literal) + 1U);
    descriptor.template_text = text;
    descriptor.template_length = strlen(literal);
    descriptor.target_machine_form_id = id;
    descriptor.target_machine_form_variant = variant;
    function.inline_asms = &descriptor;
    function.inline_asm_count = 1U;
    ctx.fn = &function;
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    assert(!minic_loongarch64_emit_qualified_structured_asm(out, &ctx, &ins));
    assert(ftell(out) == 0L);
    fclose(out);
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
    descriptor_core_type_machine_positive();
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
