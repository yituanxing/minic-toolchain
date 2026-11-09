/* Whitebox-only guard for Core's ASM ID exactly-once ownership invariant.
 * Include the implementation so the private checker can be exercised with
 * synthetic malformed Core that a valid C frontend never constructs. */
#include "target/common/direct_preflight.c"

static int check_asm_ids(size_t count) {
    MinicCoreFunction function = {0};
    MinicCoreType void_type = {.kind = MINIC_CORE_TYPE_VOID};
    MinicCoreTypeContext types = {
        .types = &void_type, .type_count = 1U, .type_capacity = 1U
    };
    MinicCoreInstruction *instructions = NULL;
    MinicCoreInlineAsm *asms = NULL;
    size_t i;
    int failed = 1;
    char nop[] = "nop";

    instructions = (MinicCoreInstruction *)calloc(count, sizeof(*instructions));
    asms = (MinicCoreInlineAsm *)calloc(count, sizeof(*asms));
    if (instructions == NULL || asms == NULL) goto done;
    for (i = 0U; i < count; ++i) {
        instructions[i].kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        /* Only the sealed Core void identity authorizes an ASM effect.
         * C descriptors do not exist in the executable Core instruction. */
        instructions[i].core_type_id = 0U;
        instructions[i].result = MINIC_CORE_VALUE_INVALID;
        instructions[i].value.structured_inline_asm.inline_asm_id = (MinicCoreInlineAsmId)i;
        asms[i].template_text = nop;
        asms[i].template_length = 3U;
    }
    function.inline_asm_count = count;
    function.type_context = &types;
    function.inline_asms = asms;
    function.instruction_count = count;
    function.instructions = instructions;
    if (!direct_lp64_only_structured_asm(&function, NULL)) goto done;

    /* Source-generic scalar fallbacks were previously RISC-V-only opcodes.
     * Their new typed STRUCTURED spelling must not silently widen x86/A64/
     * Loong hardware capabilities, while target-qualified exact forms and
     * pre-existing generic +r/two-input effects remain admissible here. */
    instructions[0].value.structured_inline_asm.operand_count = 1U;
    instructions[0].value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (direct_lp64_only_structured_asm(&function, NULL)) goto done;
    asms[0].operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    if (!direct_lp64_only_structured_asm(&function, NULL)) goto done;
    asms[0].operand_plan_owner = MINIC_ASM_OPERAND_PLAN_NONE;
    instructions[0].value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    if (!direct_lp64_only_structured_asm(&function, NULL)) goto done;
    instructions[0].value.structured_inline_asm.operand_count = 2U;
    instructions[0].value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    instructions[0].value.structured_inline_asm.operands[1].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (direct_lp64_only_structured_asm(&function, NULL)) goto done;
    instructions[0].value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (!direct_lp64_only_structured_asm(&function, NULL)) goto done;
    instructions[0].value.structured_inline_asm.operand_count = 0U;

    /* One repeated ID leaves another entry orphaned. */
    if (count > 1U) {
        instructions[count - 1U].value.structured_inline_asm.inline_asm_id = 0U;
        if (direct_lp64_only_structured_asm(&function, NULL)) goto done;
    }
    /* A bad ID must be rejected before the bitmap is indexed. */
    instructions[count - 1U].value.structured_inline_asm.inline_asm_id =
        (MinicCoreInlineAsmId)count;
    if (direct_lp64_only_structured_asm(&function, NULL)) goto done;
    instructions[count - 1U].value.structured_inline_asm.inline_asm_id =
        (MinicCoreInlineAsmId)(count - 1U);
    function.instruction_count = count - 1U;
    if (direct_lp64_only_structured_asm(&function, NULL)) goto done;
    function.instruction_count = count;
    asms[0].template_text = NULL;
    if (direct_lp64_only_structured_asm(&function, NULL)) goto done;

    failed = 0;
done:
    free(instructions);
    free(asms);
    return failed;
}

int main(void) {
    if (check_asm_ids(1U) != 0 ||
        check_asm_ids(256U) != 0 ||
        check_asm_ids(257U) != 0 ||
        check_asm_ids(512U) != 0) {
        return 1;
    }
    return 0;
}
