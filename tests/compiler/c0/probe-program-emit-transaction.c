#include "target/common/program_emit.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standalone host-level probe of the real five-target output transaction. */
typedef struct TestContext {
    unsigned int fail_stage, globals, functions, finalizations;
} TestContext;

static bool prefix(FILE *out, const MinicC0Program *p,
                   const MinicTargetInfo *t, const MinicCoreFunction *f,
                   size_t n, bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)f; (void)n; (void)sections;
    return fputs("P\n", out) != EOF && ctx->fail_stage != 1U;
}
static bool global(FILE *out, const MinicC0Program *p,
                   const MinicTargetInfo *t, const MinicGlobalObject *obj,
                   size_t index, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)obj;
    if (index != 0U) return false;
    ++ctx->globals;
    return fputs("G\n", out) != EOF && ctx->fail_stage != 2U;
}
static bool between(FILE *out, const MinicC0Program *p,
                    const MinicTargetInfo *t, const MinicCoreFunction *f,
                    size_t n, bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)f; (void)n; (void)sections;
    return fputs("B\n", out) != EOF && ctx->fail_stage != 3U;
}
static bool function(FILE *out, const MinicC0Program *p,
                     const MinicTargetInfo *t, const MinicFunction *decl,
                     const MinicCoreFunction *f,
                     const MinicCoreFunctionFacts *facts,
                     const MinicCoreObjectStoragePlan *storage, size_t index,
                     bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)decl; (void)f; (void)sections;
    if (index != 0U || facts == NULL || storage == NULL ||
        storage->count != 0U ||
        facts->direct_call_count != 1U ||
        facts->indirect_call_count != 1U ||
        facts->variadic_address_count != 1U ||
        facts->variadic_address_results == NULL ||
        facts->variadic_address_results[0] != 0U ||
        !facts->needs_saved_return_address ||
        !facts->uses_structured_inline_asm ||
        !facts->uses_dynamic_stack_alloc ||
        facts->max_call_arity != 7U || facts->call_index == NULL ||
        facts->verified_function != f)
        return false;
    ++ctx->functions;
    return fputs("F\n", out) != EOF && ctx->fail_stage != 4U;
}
static bool finalize(const char *path, const MinicTargetInfo *t,
                     void *opaque) {
    TestContext *ctx = opaque;
    FILE *in;
    char data[64] = {0};
    size_t bytes;
    (void)t;
    ++ctx->finalizations;
    if (strstr(path, ".minic-") == NULL) return false;
    in = fopen(path, "rb");
    if (in == NULL) return false;
    bytes = fread(data, 1U, sizeof(data) - 1U, in);
    if (fclose(in) != 0) return false;
    return bytes == strlen("P\nG\nB\nF\nT\n") &&
           strcmp(data, "P\nG\nB\nF\nT\n") == 0 &&
           ctx->fail_stage != 5U;
}
int main(int argc, char **argv) {
    MinicGlobalObject object = {0};
    MinicFunction declaration = {0};
    MinicCoreFunction ir = {0};
    /* Transaction callbacks use six global instructions for fact coverage,
     * but executable Core must still have a valid, reachable entry block. */
    MinicCoreBlock entry_block = {
        .terminator = { .kind = MINIC_CORE_TERMINATOR_RETURN },
        .has_terminator = true,
        .source_label_id = SIZE_MAX
    };
    MinicCoreInstruction instructions[6] = {
        { .kind = MINIC_CORE_INSTRUCTION_CALL, .value.call = { .argument_count = 7U } },
        { .kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL, .value.indirect_call = { .argument_count = 3U } },
        { .kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC },
        { .kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM },
        { .kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS },
        { .kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS }
    };
    MinicC0Program program = {0};
    MinicTargetInfo target = {0};
    MinicProgramEmitConfig config = {
        global, function, prefix, between, "T\n", finalize,
        MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    TestContext ctx = {0};
    unsigned int stage;
    const char *path;
    if (argc != 2) return 2;
    path = argv[1];
    /* Real AST constructors initialize an unowned file-scope object with
     * INVALID. Do not rely on a zero-initialized synthetic test shortcut. */
    object.owner_function = MINIC_FUNCTION_INVALID;
    declaration.is_defined = true;
    instructions[5].value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
    ir.instructions = instructions;
    ir.instruction_count = sizeof(instructions) / sizeof(instructions[0]);
    ir.blocks = &entry_block;
    ir.block_count = 1U;
    ir.entry_block = 0U;
    program.global_objects = &object;
    program.global_object_count = 1U;
    target.data_layout = &(MinicDataLayout){0};
    program.functions = &declaration;
    program.function_count = 1U;
    for (stage = 0U; stage <= 5U; ++stage) {
        FILE *existing;
        bool result;
        (void)remove(path);
        (void)memset(&ctx, 0, sizeof(ctx));
        ctx.fail_stage = stage;
        result = minic_program_emit_write(
            path, &program, &target, &ir, 1U, false, &config, &ctx);
        existing = fopen(path, "rb");
        if (result != (stage == 0U) ||
            (existing != NULL) != (stage == 0U) ||
            ctx.globals != (stage == 1U ? 0U : 1U) ||
            ctx.functions != (stage == 0U || stage >= 4U ? 1U : 0U)) {
            if (existing != NULL) (void)fclose(existing);
            return (int)stage + 10;
        }
        if (existing != NULL) (void)fclose(existing);
    }
    /* Census keeps scanning after a failed global, but leaves no partial .s. */
    config.continue_after_failure = true;
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.fail_stage = 2U;
    (void)remove(path);
    if (minic_program_emit_write(
            path, &program, &target, &ir, 1U, false,
            &config, &ctx) || ctx.globals != 1U || ctx.functions != 1U)
        return 20;
    {
        FILE *leaked = fopen(path, "rb");
        if (leaked != NULL) {
            (void)fclose(leaked);
            return 21;
        }
    }
    (void)remove(path);
    /* Errors at every stage must leave the previous successful output
     * untouched; this distinguishes atomic replacement from unlink rollback. */
    config.continue_after_failure = false;
    for (stage = 1U; stage <= 5U; ++stage) {
        FILE *original = fopen(path, "wb");
        FILE *existing;
        char data[32] = {0};
        size_t n;
        if (original == NULL || fputs("ORIGINAL\n", original) == EOF ||
            fclose(original) != 0) return 30;
        (void)memset(&ctx, 0, sizeof(ctx));
        ctx.fail_stage = stage;
        if (minic_program_emit_write(path, &program, &target, &ir, 1U,
                                      false, &config, &ctx)) return 31;
        existing = fopen(path, "rb");
        if (existing == NULL) return 32;
        n = fread(data, 1U, sizeof(data) - 1U, existing);
        if (fclose(existing) != 0 || n != strlen("ORIGINAL\n") ||
            strcmp(data, "ORIGINAL\n") != 0) return 33;
    }
    (void)memset(&ctx, 0, sizeof(ctx));
    if (!minic_program_emit_write(path, &program, &target, &ir, 1U,
                                  false, &config, &ctx)) return 34;
    {
        FILE *in = fopen(path, "rb");
        char data[64] = {0};
        if (in == NULL || fread(data, 1U, sizeof(data) - 1U, in) !=
                          strlen("P\nG\nB\nF\nT\n") ||
            fclose(in) != 0 || strcmp(data, "P\nG\nB\nF\nT\n") != 0)
            return 35;
    }
    (void)remove(path);
    puts("MINIC_FIVE_TARGET_OUTPUT_TRANSACTION=PASS failures=5 rollback=5 census=1 atomic_preserve=5");
    return 0;
}

/* The host-native transaction probe has no type-layout dependency. For
 * zero-object test functions, the real builder and this small test stub
 * have identical success/cleanup semantics. Architecture runtime gates
 * always link the production Core storage implementation. */
bool minic_core_object_storage_plan_build(
    const MinicC0Program *program, const MinicDataLayout *layout,
    const MinicCoreFunction *fn, MinicCoreObjectStoragePlan *plan) {
    return program != NULL && layout != NULL && fn != NULL && plan != NULL &&
           fn->object_count == 0U && plan->objects == NULL;
}
void minic_core_object_storage_plan_destroy(MinicCoreObjectStoragePlan *plan) {
    if (plan != NULL) *plan = (MinicCoreObjectStoragePlan){0};
}

/* The transaction test deliberately uses synthetic, not semantically valid,
 * CALL instructions to test rollback after failed output stages. Its host
 * stub is isolated from the real separately tested semantic CALL index. */
bool minic_core_call_index_build(const MinicCoreFunction *fn,
                                 size_t expected, MinicCoreCallIndex *index) {
    return fn != NULL && expected == 2U && index != NULL &&
           index->sites == NULL && index->count == 0U;
}
void minic_core_call_index_destroy(MinicCoreCallIndex *index) {
    if (index != NULL) *index = (MinicCoreCallIndex){0};
}

/* The transaction test uses intentionally synthetic CALL operands. Its
 * standalone verifier stub tests output lifetime without linking the real
 * Core verifier, which is covered by product compiler regression gates. */
bool minic_core_function_verify(const MinicCoreFunction *fn) {
    return fn != NULL && fn->instruction_count == 6U &&
           fn->instructions != NULL;
}
