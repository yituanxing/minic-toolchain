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
                     const MinicCoreFunctionFacts *facts, size_t index,
                     bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)decl; (void)f; (void)sections;
    if (index != 0U || facts == NULL ||
        facts->direct_call_count != 1U ||
        facts->indirect_call_count != 1U ||
        facts->variadic_address_count != 1U ||
        !facts->needs_saved_return_address ||
        !facts->uses_structured_inline_asm ||
        !facts->uses_dynamic_stack_alloc)
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
    MinicCoreInstruction instructions[6] = {
        { .kind = MINIC_CORE_INSTRUCTION_CALL },
        { .kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL },
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
    declaration.is_defined = true;
    instructions[5].value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
    ir.instructions = instructions;
    ir.instruction_count = sizeof(instructions) / sizeof(instructions[0]);
    program.global_objects = &object;
    program.global_object_count = 1U;
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
    puts("MINIC_FIVE_TARGET_OUTPUT_TRANSACTION=PASS failures=5 rollback=5 census=1");
    return 0;
}
