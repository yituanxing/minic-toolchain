/* Whitebox-only guard for Core's ASM ID exactly-once ownership invariant.
 * Include the implementation so the private checker can be exercised with
 * synthetic malformed Core that a valid C frontend never constructs. */
#include "target/common/direct_preflight.c"

static int check_asm_ids(size_t count) {
    MinicCoreFunction function = {0};
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
        instructions[i].type = minic_type_void();
        instructions[i].result = MINIC_CORE_VALUE_INVALID;
        instructions[i].value.structured_inline_asm.inline_asm_id = (MinicCoreInlineAsmId)i;
        asms[i].template_text = nop;
        asms[i].template_length = 3U;
    }
    function.inline_asm_count = count;
    function.inline_asms = asms;
    function.instruction_count = count;
    function.instructions = instructions;
    if (!direct_lp64_only_structured_asm(&function, NULL)) goto done;

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
