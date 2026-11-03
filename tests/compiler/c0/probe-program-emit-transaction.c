#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standalone host-level probe of the real five-target output transaction. */
typedef struct TestContext {
    unsigned int fail_stage, globals, functions, finalizations;
} TestContext;

static bool prefix(FILE *out, const MinicCoreModule *p,
                   const MinicTargetInfo *t, const MinicCoreFunction *f,
                   size_t n, bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)f; (void)n; (void)sections;
    return fputs("P\n", out) != EOF && ctx->fail_stage != 1U;
}
static bool fixture_global(FILE *out, const MinicCoreModule *p,
                   const MinicTargetInfo *t, const MinicCoreModuleGlobal *obj,
                   size_t index, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)obj;
    if (index != 0U) return false;
    ++ctx->globals;
    return fputs("G\n", out) != EOF && ctx->fail_stage != 2U;
}
static bool between(FILE *out, const MinicCoreModule *p,
                    const MinicTargetInfo *t, const MinicCoreFunction *f,
                    size_t n, bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)f; (void)n; (void)sections;
    return fputs("B\n", out) != EOF && ctx->fail_stage != 3U;
}
static bool function(FILE *out, const MinicCoreModule *p,
                     const MinicTargetInfo *t,
                     const MinicCoreModuleFunctionSymbol *module_decl,
                     const MinicCoreFunction *f,
                     const MinicCoreFunctionFacts *facts,
                     const MinicCoreObjectStoragePlan *storage, size_t index,
                     bool sections, void *opaque) {
    TestContext *ctx = opaque;
    (void)p; (void)t; (void)f; (void)sections;
    if (p == NULL || module_decl == NULL) return false;
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
        facts->verified_function != f ||
        facts->reachable_blocks == NULL || !facts->reachable_blocks[0] ||
        facts->value_use_counts == NULL)
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

typedef struct ModuleTestContext {
    const MinicCoreModule *module;
    size_t globals;
} ModuleTestContext;

static bool module_global(FILE *out, const MinicCoreModule *module,
                          const MinicTargetInfo *target,
                          const MinicCoreModuleGlobal *object,
                          size_t ordinal, void *opaque) {
    ModuleTestContext *context = opaque;
    (void)target;
    if (module != context->module || ordinal != 0U || object == NULL ||
        object->name == NULL || strcmp(object->name, "owned") != 0)
        return false;
    ++context->globals;
    return fprintf(out, "G:%s\n", object->name) >= 0;
}

static bool module_between(FILE *out, const MinicCoreModule *module,
                           const MinicTargetInfo *target,
                           const MinicCoreFunction *functions,
                           size_t function_count, bool sections, void *opaque) {
    const ModuleTestContext *context = opaque;
    (void)target; (void)functions; (void)function_count;
    (void)sections;
    return module == context->module &&
           minic_program_emit_source_file_asm_module(out, module);
}

/* Module-only output owns immutable global/ASM and symbol-use evidence. */
static int check_module_snapshot_output(const char *path) {
    MinicCoreModuleFragment fragment = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
        .byte_count = 8U,
        .relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT,
        .relocation_symbol = "weak_ext"
    };
    MinicCoreModuleGlobal globals[4] = {
        { .name = "owned", .emittable = true, .fragments = &fragment,
          .fragment_count = 1U },
        { .name = "weak_ext", .is_extern = true, .is_weak = true,
          .visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN },
        { .name = "unused_hidden", .is_extern = true,
          .visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN },
        { .name = "asm_weak", .is_extern = true, .is_weak = true,
          .visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN }
    };
    MinicCoreModuleFileAsm file_asm = { " .quad asm_weak", 15U };
    MinicCoreModule module = {
        .globals = globals, .global_count = 4U,
        .file_asms = &file_asm, .file_asm_count = 1U,
        .finalized = true
    };
    MinicTargetInfo target = {0};
    MinicProgramEmitConfig config = {
        .emit_function = function,
        .between = module_between,
        .trailer = "T\n",
        .selection = MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS
    };
    ModuleTestContext context = { .module = &module };
    FILE *in;
    char data[4096] = {0};
    size_t count;
    if (!minic_program_emit_write_module(
            path, &module, &target, NULL, 0U, false,
            &config, &context, module_global)) return 1;
    in = fopen(path, "rb");
    if (in == NULL) return 2;
    count = fread(data, 1U, sizeof(data) - 1U, in);
    if (fclose(in) != 0 || count == 0U || context.globals != 1U ||
        strstr(data, ".weak weak_ext\n.hidden weak_ext\n") == NULL ||
        strstr(data, ".weak asm_weak\n.hidden asm_weak\n") == NULL ||
        strstr(data, "G:owned\n") == NULL ||
        strstr(data, ".text\n .quad asm_weak\n.text\n") == NULL ||
        strstr(data, "unused_hidden") != NULL ||
        strstr(data, "poison_source") != NULL) return 3;
    /* Repeated output is deterministic from the same sealed module. */
    context.globals = 0U;
    if (!minic_program_emit_write_module(
            path, &module, &target, NULL, 0U, false,
            &config, &context, module_global)) return 12;
    in = fopen(path, "rb");
    if (in == NULL) return 13;
    (void)memset(data, 0, sizeof(data));
    count = fread(data, 1U, sizeof(data) - 1U, in);
    if (fclose(in) != 0 || count == 0U || context.globals != 1U ||
        strstr(data, ".weak weak_ext\n.hidden weak_ext\n") == NULL ||
        strstr(data, ".weak asm_weak\n.hidden asm_weak\n") == NULL ||
        strstr(data, "G:owned\n") == NULL ||
        strstr(data, ".text\n .quad asm_weak\n.text\n") == NULL ||
        strstr(data, "poison_source") != NULL) return 14;
    /* The emitted .weak/.hidden directives and unused-symbol exclusion above
     * exercise the real private symbol-use collector through its output. */
    module.finalized = false;
    if (minic_program_emit_write_module(
            path, &module, &target, NULL, 0U, false,
            &config, &context, module_global)) return 4;
    module.finalized = true;
    fragment.relocation_symbol = NULL;
    if (minic_program_emit_write_module(
            path, &module, &target, NULL, 0U, false,
            &config, &context, module_global)) return 8;
    fragment.relocation_symbol = "weak_ext";
    {
        MinicCoreModuleFunctionSymbol symbols[2] = {
            { .name = "real_fn", .is_defined = true, .alias_target_index = SIZE_MAX },
            { .name = "alias_fn", .alias_target_index = 0U,
              .alias_target_name = "real_fn" }
        };
        MinicCoreModuleGlobal alias = {
            .name = "alias_obj", .is_extern = true, .is_alias = true,
            .alias_target_index = 0U, .alias_target_name = "owned"
        };
        FILE *tmp = tmpfile();
        if (tmp == NULL) return 5;
        module.function_symbols = symbols;
        module.function_count = 2U;
        if (!minic_multi_elf_emit_module_object_alias(
                tmp, &module, &alias, "@object") ||
            !minic_multi_elf_emit_module_function_aliases(
                tmp, &module, "@function") || fseek(tmp, 0L, SEEK_SET) != 0)
            { (void)fclose(tmp); return 6; }
        (void)memset(data, 0, sizeof(data));
        count = fread(data, 1U, sizeof(data) - 1U, tmp);
        if (fclose(tmp) != 0 || count == 0U ||
            strstr(data, ".set alias_obj, owned\n") == NULL ||
            strstr(data, ".set alias_fn, real_fn\n") == NULL) return 7;
    }
    (void)remove(path);
    return 0;
}

int main(int argc, char **argv) {
    MinicCoreModuleGlobal object = {
        .name = "fixture_global", .emittable = true
    };
    MinicCoreModuleFunctionSymbol declaration = {
        .name = "fixture_function", .is_defined = true,
        .is_referenced = true, .alias_target_index = SIZE_MAX
    };
    MinicCoreFunction ir = {0};
    MinicCoreTypeContext synthetic_types = {0};
    /* Transaction callbacks use six global instructions for fact coverage,
     * but executable Core must still have a valid, reachable entry block. */
    MinicCoreBlock entry_block = {
        .terminator = { .kind = MINIC_CORE_TERMINATOR_RETURN,
                        .return_value = MINIC_CORE_VALUE_INVALID },
        .has_terminator = true
    };
    MinicCoreInstruction instructions[6] = {
        { .kind = MINIC_CORE_INSTRUCTION_CALL, .value.call = { .argument_count = 7U } },
        { .kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL, .value.indirect_call = { .argument_count = 3U } },
        { .kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC },
        { .kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM },
        { .kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS },
        { .kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS }
    };
    MinicCoreModule module = {
        .globals = &object, .global_count = 1U,
        .function_symbols = &declaration, .function_count = 1U,
        .finalized = true
    };
    MinicTargetInfo target = {0};
    MinicProgramEmitConfig config = {
        .emit_function = function,
        .prefix = prefix,
        .between = between,
        .trailer = "T\n",
        .finalize = finalize,
        .selection = MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS,
        .continue_after_failure = false,
    };
    TestContext ctx = {0};
    unsigned int stage;
    const char *path;
    if (argc != 2) return 2;
    path = argv[1];
    instructions[5].value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
    ir.instructions = instructions;
    ir.type_context = &synthetic_types;
    ir.instruction_count = sizeof(instructions) / sizeof(instructions[0]);
    ir.blocks = &entry_block;
    ir.block_count = 1U;
    ir.entry_block = 0U;
    target.data_layout = &(MinicDataLayout){0};
    for (stage = 0U; stage <= 5U; ++stage) {
        FILE *existing;
        bool result;
        (void)remove(path);
        (void)memset(&ctx, 0, sizeof(ctx));
        ctx.fail_stage = stage;
        result = minic_program_emit_write_module(
            path, &module, &target, &ir, 1U, false, &config, &ctx,
            fixture_global);
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
    if (minic_program_emit_write_module(
            path, &module, &target, &ir, 1U, false,
            &config, &ctx, fixture_global) ||
        ctx.globals != 1U || ctx.functions != 1U)
        return 20;
    {
        FILE *leaked = fopen(path, "rb");
        if (leaked != NULL) {
            (void)fclose(leaked);
            return 21;
        }
    }
    /* Module definitions select the shared Core function-plan builder. */
    {
        MinicCoreModuleFunctionSymbol module_decl = {
            .name = "sealed_function", .is_defined = true, .is_referenced = true,
            .alias_target_index = SIZE_MAX
        };
        MinicCoreModule owned_module = {
            .function_symbols = &module_decl, .function_count = 1U,
            .finalized = true
        };
        FILE *probe = tmpfile();
        if (probe == NULL) return 41;
        (void)memset(&ctx, 0, sizeof(ctx));
        if (!minic_program_visit_prepared_functions_module(
                probe, &owned_module, &target, &ir, 1U, false,
                MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
                function, &ctx) || ctx.functions != 1U) {
            (void)fclose(probe);
            return 42;
        }
        if (fclose(probe) != 0) return 43;
        /* A missing sealed Module cannot sneak through the machine visitor. */
        probe = tmpfile();
        if (probe == NULL) return 44;
        (void)memset(&ctx, 0, sizeof(ctx));
        if (minic_program_visit_prepared_functions_module(
                probe, NULL, &target, &ir, 1U, false,
                MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
                function, &ctx) || ctx.functions != 0U) {
            (void)fclose(probe);
            return 45;
        }
        if (fclose(probe) != 0) return 46;
    }
    if (check_module_snapshot_output(path) != 0) return 50;
    puts("MINIC_CORE_MODULE_SYMBOL_TRANSACTION=PASS no_C_program_global=file_asm=1 weak_hidden=2 alias=2 invalid_reject=2");
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
        if (minic_program_emit_write_module(path, &module, &target, &ir, 1U,
                                      false, &config, &ctx,
                                      fixture_global)) return 31;
        existing = fopen(path, "rb");
        if (existing == NULL) return 32;
        n = fread(data, 1U, sizeof(data) - 1U, existing);
        if (fclose(existing) != 0 || n != strlen("ORIGINAL\n") ||
            strcmp(data, "ORIGINAL\n") != 0) return 33;
    }
    (void)memset(&ctx, 0, sizeof(ctx));
    if (!minic_program_emit_write_module(path, &module, &target, &ir, 1U,
                                  false, &config, &ctx, fixture_global)) return 34;
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
    const MinicCoreFunction *fn, MinicCoreObjectStoragePlan *plan) {
    return fn != NULL && plan != NULL &&
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
