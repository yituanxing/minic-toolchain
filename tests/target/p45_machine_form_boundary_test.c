/* P45: a GNU C source form is not itself permission to emit an opcode.
 * SourceBackend proves the GNU operand/effect contract, Core transports
 * machine form + variant, and the ISA backend checks the form again.
 * A manufactured Core ID, mismatched variant, wrong owner or corrupt Core
 * type must fail without writing even a partial assembly instruction.
 *
 * This is a host unit test: compile with the MiniC object set, omitting the
 * minic-cc CLI main (the existing LOONG_TYPED_DESCRIPTOR_TEST_OBJECTS set).
 */
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/backend_source.h"
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/x86_64/asm_contract.h"
#include "target/x86_64/machine_internal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum ProbeArch { PROBE_X86, PROBE_A64 } ProbeArch;

static bool register_integer(MinicCoreTypeContext *types, size_t bytes,
                             MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.integer.bit_width = (unsigned int)(bytes * 8U);
    type.value.integer.is_signed = false;
    return minic_core_type_register(types, &type, id);
}

static bool register_pointer(MinicCoreTypeContext *types,
                             MinicCoreTypeId pointee, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(types, &type, id);
}

static void source_operand(MinicInlineAsmOperand *op, const char *constraint,
                           MinicExpressionId expression,
                           MinicInlineAsmOperandAccess access) {
    memset(op, 0, sizeof(*op));
    op->constraint_text = (char *)constraint;
    op->constraint_length = strlen(constraint);
    op->expression = expression;
    op->access = access;
}

/* Exercise actual source registration, including C lvalue/rank checks,
 * instead of manufacturing a correct machine ID inside this test. */
static MinicTargetInlineAsmContract source_qualify(ProbeArch arch) {
    const MinicTargetArchitecture architecture = arch == PROBE_X86
        ? MINIC_TARGET_ARCH_X86_64 : MINIC_TARGET_ARCH_AARCH64;
    MinicTargetInvocation invocation;
    MinicC0Program program = {0};
    MinicExpression expressions[2] = {{0}, {0}};
    MinicInlineAsm asm_source = {0};
    MinicInlineAsmOperand output = {0}, input = {0};
    MinicTargetInlineAsmContract contract = {0};
    const MinicSourceBackend *backend;
    const char *template_text = arch == PROBE_X86
        ? "lfence" : "ldar %w0, %1";

    assert(minic_target_invocation_begin(
        &invocation, minic_target_info_for_architecture(architecture)));
    backend = minic_source_backend_for_target(&invocation.machine);
    assert(backend != NULL);
    asm_source.template_text = (char *)template_text;
    asm_source.template_length = strlen(template_text);
    asm_source.is_volatile = true;
    asm_source.clobber_count = 1U;
    asm_source.has_memory_clobber = true;
    if (arch == PROBE_A64) {
        program.expressions = expressions;
        program.expression_count = 2U;
        expressions[0].type = minic_type_unsigned_int();
        expressions[0].value_category = MINIC_VALUE_LVALUE;
        expressions[1].type = minic_type_unsigned_int();
        expressions[1].value_category = MINIC_VALUE_LVALUE;
        source_operand(&output, "=r", 0U,
                       MINIC_INLINE_ASM_OPERAND_WRITE_ONLY);
        source_operand(&input, "Q", 1U,
                       MINIC_INLINE_ASM_OPERAND_READ_ONLY);
        asm_source.outputs = &output;
        asm_source.output_count = 1U;
        asm_source.inputs = &input;
        asm_source.input_count = 1U;
    }
    assert(minic_source_backend_qualify_inline_asm(
        backend, &invocation, &program, &asm_source, &contract));
    assert(minic_source_backend_seal_normalized_inline_asm(
        backend, &invocation, &program, &asm_source,
        template_text, strlen(template_text), &contract));
    assert(contract.owner == MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED);
    assert(contract.target_machine_form_id == (arch == PROBE_X86
        ? MINIC_X86_MACHINE_ASM_ZERO_BARRIER : AARCH64_MACHINE_ASM_LDAR));
    assert(contract.target_machine_form_variant ==
           (arch == PROBE_X86 ? 3U : 4U));
    assert(contract.operand_count == (arch == PROBE_X86 ? 0U : 2U));
    return contract;
}

static bool emit_one(ProbeArch arch, const MinicCoreFunction *function,
                     const MinicCoreInstruction *instruction,
                     char *output, size_t capacity) {
    MinicDirectLP64BlockContext a64 = {0};
    MinicLP64FramePlan a64_frame = {0};
    MinicX86Frame x86_frame = {0};
    MinicCoreModule module = {.finalized = true};
    MinicTargetInfo target = {0};
    size_t offsets[2] = {16U, 24U};
    FILE *stream = tmpfile();
    size_t size;
    bool result;
    assert(output != NULL && capacity > 1U && stream != NULL);
    output[0] = '\0';
    a64_frame.value_count = 2U;
    a64_frame.value_offsets = offsets;
    a64.fn = function;
    a64.frame_plan = &a64_frame;
    a64.frame = 64U;
    x86_frame.value_count = 2U;
    x86_frame.value_offsets = offsets;
    result = arch == PROBE_A64
        ? minic_aarch64_emit_qualified_structured_asm(stream, &a64, instruction)
        : minic_x86_64_emit_structured_asm(stream, &module, &target,
            function, &x86_frame, instruction, 0U);
    assert(fflush(stream) == 0 && fseek(stream, 0L, SEEK_SET) == 0);
    size = fread(output, 1U, capacity - 1U, stream);
    assert(!ferror(stream));
    assert(size < capacity - 1U || fgetc(stream) == EOF);
    output[size] = '\0';
    assert(fclose(stream) == 0);
    return result;
}

static void must_reject(ProbeArch arch, const char *reason,
                        const MinicCoreFunction *fn,
                        const MinicCoreInstruction *ins) {
    char output[2048];
    if (emit_one(arch, fn, ins, output, sizeof(output))) {
        fprintf(stderr, "P45 BAD MACHINE ADMISSION arch=%s poison=%s asm=%s\n",
                arch == PROBE_X86 ? "x86_64" : "aarch64", reason, output);
        assert(0 && "manufactured Core machine form must be rejected");
    }
    assert(output[0] == '\0');
}

static void check_machine_boundary(ProbeArch arch) {
    MinicTargetInlineAsmContract proof = source_qualify(arch);
    MinicCoreTypeContext types;
    MinicCoreType none = {0};
    MinicCoreTypeId void_id, u32, u64, ptr_u32, ptr_u64;
    MinicCoreValue values[2] = {{0}, {0}};
    MinicCoreFunction function = {0};
    MinicCoreInlineAsm form = {0};
    MinicCoreInstruction ins = {0};
    char positive[2048], repeated[2048];
    const char *const text = arch == PROBE_X86
        ? "lfence" : "ldar %w0, %1";

    minic_core_type_context_initialize(&types);
    none.kind = MINIC_CORE_TYPE_VOID;
    assert(minic_core_type_register(&types, &none, &void_id));
    assert(register_integer(&types, 4U, &u32));
    assert(register_integer(&types, 8U, &u64));
    assert(register_pointer(&types, u32, &ptr_u32));
    assert(register_pointer(&types, u64, &ptr_u64));
    function.type_context = &types;
    function.values = values;
    function.value_count = 2U;
    function.inline_asms = &form;
    function.inline_asm_count = 1U;
    values[0].core_type_id = ptr_u32;
    values[1].core_type_id = ptr_u32;
    form.template_text = (char *)text;
    form.template_length = strlen(text);
    form.is_volatile = true;
    form.has_memory_clobber = true;
    form.operand_plan_owner = proof.owner;
    form.target_machine_form_id = proof.target_machine_form_id;
    form.target_machine_form_variant = proof.target_machine_form_variant;
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.core_type_id = void_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    ins.value.structured_inline_asm.operand_count = proof.operand_count;
    if (arch == PROBE_A64) {
        ins.value.structured_inline_asm.operands[0].operand_index = 0U;
        ins.value.structured_inline_asm.operands[0].kind = proof.kinds[0];
        ins.value.structured_inline_asm.operands[0].value = 0U;
        ins.value.structured_inline_asm.operands[1].operand_index = 1U;
        ins.value.structured_inline_asm.operands[1].kind = proof.kinds[1];
        ins.value.structured_inline_asm.operands[1].value = 1U;
    }
    if (!emit_one(arch, &function, &ins, positive, sizeof(positive))) {
        fprintf(stderr, "P45 TYPED MACHINE REJECTED arch=%s asm=%s\n",
                arch == PROBE_X86 ? "x86_64" : "aarch64", positive);
        assert(0 && "source-sealed typed machine form must emit");
    }
    assert(strstr(positive, arch == PROBE_X86
        ? "  lfence\n" : "  ldar w9, [x10]\n") != NULL);
    assert(emit_one(arch, &function, &ins, repeated, sizeof(repeated)));
    assert(strcmp(positive, repeated) == 0);

    /* The machine emitter cannot trust a fabricated Core proof. */
    ++form.target_machine_form_variant;
    must_reject(arch, "variant", &function, &ins);
    --form.target_machine_form_variant;
    form.target_machine_form_id = UINT32_MAX;
    must_reject(arch, "id", &function, &ins);
    form.target_machine_form_id = proof.target_machine_form_id;
    form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    must_reject(arch, "owner", &function, &ins);
    form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    form.template_text = arch == PROBE_X86 ? "mfence" : "stlr %w1, %0";
    form.template_length = strlen(form.template_text);
    must_reject(arch, "template", &function, &ins);
    form.template_text = (char *)text;
    form.template_length = strlen(text);

    if (arch == PROBE_X86) {
        /* A valid x86 target-only memory-fence template is not generic
         * structured ASM. Neither a source-owned claim without a machine
         * identity nor an invented generic claim may bypass the ID seal. */
        form.target_machine_form_id = 0U;
        form.target_machine_form_variant = 0U;
        must_reject(arch, "target_only_id0", &function, &ins);
        form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
        must_reject(arch, "generic_target_only_id0", &function, &ins);
        form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        form.target_machine_form_id = proof.target_machine_form_id;
        form.target_machine_form_variant = proof.target_machine_form_variant;
    }
    if (arch == PROBE_A64) {
        values[1].core_type_id = ptr_u64;
        must_reject(arch, "core_pointee", &function, &ins);
        values[1].core_type_id = ptr_u32;
        ins.value.structured_inline_asm.operands[1].value = 10U;
        must_reject(arch, "ssa_id", &function, &ins);
        ins.value.structured_inline_asm.operands[1].value = 1U;

        /* RBIT is a valid, target-qualified GNU arm64 operation, but it
         * is NOT a generic structured ASM permission. The pre-P45 raw
         * template dispatcher would wrongly accept this exact machine
         * text with either source-qualified or generic ownership AND ID0.
         * Both forged identity cases must now fail before any output. */
        form.template_text = "rbit %w0, %w1";
        form.template_length = strlen(form.template_text);
        form.has_memory_clobber = false;
        form.target_machine_form_id = 0U;
        form.target_machine_form_variant = 0U;
        values[1].core_type_id = u32;
        ins.value.structured_inline_asm.operands[1].kind =
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        must_reject(arch, "target_only_id0", &function, &ins);
        form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
        must_reject(arch, "generic_target_only_id0", &function, &ins);

        /* The identical ISA template is valid with a proven target ID;
         * this also guards against a coarse workaround that rejects ALL
         * RBIT machine effects instead of fixing the missing ID gate. */
        form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        form.target_machine_form_id = AARCH64_MACHINE_ASM_BIT_REVERSE;
        assert(emit_one(arch, &function, &ins, positive, sizeof(positive)));
        assert(strstr(positive, "  rbit w9, w9\n") != NULL);
        values[1].core_type_id = ptr_u32;
    }

    /* Generic structured ASM owns NO machine ID and remains supported.
     * Its legal forms are independently checked by the original ISA backend.
     */
    form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    form.target_machine_form_id = 0U;
    form.target_machine_form_variant = 0U;
    if (arch == PROBE_X86) {
        form.template_text = "movq %%rbx, %0";
        form.template_length = strlen(form.template_text);
        form.has_memory_clobber = false;
        values[0].core_type_id = ptr_u64;
        ins.value.structured_inline_asm.operand_count = 1U;
        ins.value.structured_inline_asm.operands[0].operand_index = 0U;
        ins.value.structured_inline_asm.operands[0].kind =
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        ins.value.structured_inline_asm.operands[0].value = 0U;
    } else {
        form.template_text = "dmb oshst";
        form.template_length = strlen(form.template_text);
        ins.value.structured_inline_asm.operand_count = 0U;
    }
    if (!emit_one(arch, &function, &ins, positive, sizeof(positive))) {
        fprintf(stderr, "P45 GENERIC STRUCTURED REJECTED arch=%s asm=%s\n",
                arch == PROBE_X86 ? "x86_64" : "aarch64", positive);
        assert(0 && "pre-existing generic structured ISA form must survive");
    }
    assert(strstr(positive, arch == PROBE_X86
        ? "  movq %rbx, (%r11)\n" : "  dmb oshst\n") != NULL);

    /* Missing ownership is not a supported third machine-ASM dialect;
     * Core's ID0 invariant also forbids an orphan nonzero variant. Even
     * these legitimate generic spellings must fail when metadata is forged.
     */
    form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_NONE;
    must_reject(arch, "unowned_id0", &function, &ins);
    form.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    form.target_machine_form_variant = 99U;
    must_reject(arch, "id0_nonzero_variant", &function, &ins);
    minic_core_type_context_destroy(&types);
}

int main(void) {
    check_machine_boundary(PROBE_A64);
    check_machine_boundary(PROBE_X86);
    puts("P45_MACHINE_FORM_BOUNDARY=PASS target_forms=2 typed_source_seals=2 "
         "machine_positive=2 deterministic=2 poisoned_id=2 poisoned_variant=2 "
         "poisoned_owner=2 poisoned_template=2 poisoned_core=2 "
         "target_only_id0_reject=4 a64_rbit_typed=1 "
         "generic_structured_id0=2 orphan_owner_reject=2 "
         "id0_variant_reject=2 partial_output=0");
    return 0;
}
