/* Standalone, C-frontend-free witness for the physical ELF output contract:
 * undefined weak prototypes never demote an emitted strong owner; a direct
 * LP64 machine writer must not serialize TLS storage as ordinary .data. */
#include "target/common/program_emit.h"
#include "target/common/direct_program_emit.h"
#include "core/module_ir.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool emit_object(FILE *out, const MinicCoreModule *module,
                        const MinicTargetInfo *target,
                        const MinicCoreModuleGlobal *object, size_t ordinal,
                        void *context) {
    (void)module; (void)target; (void)ordinal; (void)context;
    return object != NULL && object->name != NULL &&
        fprintf(out, ".data\n.globl %s\n.type %s,@object\n%s:\n"
                     "  .quad 0\n.size %s,8\n",
                object->name, object->name, object->name, object->name) >= 0;
}

static bool emit_function(FILE *out, const MinicCoreModule *module,
                          const MinicTargetInfo *target,
                          const MinicCoreModuleFunctionSymbol *symbol,
                          const MinicCoreFunction *function,
                          const MinicCoreFunctionFacts *facts,
                          const MinicCoreObjectStoragePlan *storage,
                          size_t ordinal, bool sections, void *context) {
    (void)module; (void)target; (void)function; (void)facts; (void)storage;
    (void)ordinal; (void)sections; (void)context;
    return symbol != NULL && symbol->is_defined &&
        fprintf(out, ".text\n.globl %s\n.type %s,@function\n%s:\n"
                     "  ret\n.size %s,.-%s\n",
                symbol->name, symbol->name, symbol->name,
                symbol->name, symbol->name) >= 0;
}

static bool emit_file_asm(FILE *out, const MinicCoreModule *module,
                          const MinicTargetInfo *target,
                          const MinicCoreFunction *functions,
                          size_t count, bool sections, void *context) {
    (void)target; (void)functions; (void)count; (void)sections; (void)context;
    return minic_program_emit_source_file_asm_module(out, module);
}

static bool direct_unused_function(FILE *out, const MinicTargetInfo *target,
                                   const MinicCoreModule *module,
                                   const MinicCoreModuleFunctionSymbol *symbol,
                                   const MinicCoreFunction *function,
                                   const MinicCoreFunctionFacts *facts,
                                   const MinicCoreObjectStoragePlan *storage,
                                   size_t index, bool sections) {
    (void)out; (void)target; (void)module; (void)symbol; (void)function;
    (void)facts; (void)storage; (void)index; (void)sections;
    return false;
}

static bool file_is(const char *path, const char *expected) {
    char buffer[128] = {0};
    FILE *file = fopen(path, "rb");
    size_t length;
    if (file == NULL) return false;
    length = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    return fclose(file) == 0 && length == strlen(expected) &&
           strcmp(buffer, expected) == 0;
}

static bool write_original(const char *path) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    return fputs("ORIGINAL\n", file) != EOF && fclose(file) == 0;
}

static bool check_direct_tls(const char *path, const MinicCoreTypeContext *types,
                             MinicCoreTypeId word_id) {
    MinicCoreModuleFragment zero = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_count = 8U
    };
    MinicCoreModuleGlobal data = {
        .name = "core_tls_owner", .emittable = true,
        .is_thread_local = true,
        .core_type_id = word_id, .storage_size = 8U,
        .storage_alignment = 8U, .fragments = &zero, .fragment_count = 1U,
        .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX,
        .default_section = MINIC_CORE_MODULE_SECTION_DATA
    };
    MinicCoreModule module = {
        .type_context = types, .globals = &data,
        .global_count = 1U, .finalized = true
    };
    MinicTargetInfo target = {0};
    MinicMultiDirectModuleConfig config = {
        .architecture_name = "aarch64", .display_name = "AArch64",
        .gnu_stack_note = ".section .note.GNU-stack,\"\",@progbits\n",
        .elf_function_type = "%function", .elf_object_type = "%object",
        .emit_function = direct_unused_function
    };
    MinicDiagnostic diagnostic = {0};
    size_t index;
    const MinicTargetArchitecture architectures[2] = {
        MINIC_TARGET_ARCH_AARCH64, MINIC_TARGET_ARCH_LOONGARCH64
    };
    if (!minic_core_module_verify(&module)) return false;
    for (index = 0U; index < 2U; ++index) {
        target.architecture = architectures[index];
        config.architecture = architectures[index];
        config.architecture_name = index == 0U ? "aarch64" : "loongarch64";
        config.display_name = index == 0U ? "AArch64" : "LoongArch64";
        if (!write_original(path) ||
            minic_multi_direct_write_module(path, &target, NULL, 0U, false,
                                           &module, &config, &diagnostic) ||
            !file_is(path, "ORIGINAL\n"))
            return false;
    }
    /* The exact same source-free Core definition must emit when non-TLS;
     * otherwise a generic output failure could masquerade as the TLS gate. */
    data.is_thread_local = false;
    for (index = 0U; index < 2U; ++index) {
        FILE *out;
        char buffer[1024] = {0};
        size_t length;
        target.architecture = architectures[index];
        config.architecture = architectures[index];
        if (!minic_multi_direct_write_module(path, &target, NULL, 0U, false,
                                             &module, &config, &diagnostic))
            return false;
        out = fopen(path, "rb");
        if (out == NULL) return false;
        length = fread(buffer, 1U, sizeof(buffer) - 1U, out);
        if (fclose(out) != 0 || length == 0U ||
            strstr(buffer, "core_tls_owner:\n") == NULL ||
            strstr(buffer, ".size core_tls_owner,8\n") == NULL)
            return false;
    }
    return true;
}

int main(int argc, char **argv) {
    static const char file_asm_text[] =
        ".data\n.quad weak_before\n.quad weak_after\n.quad weak_fn\n"
        ".quad weak_after_fn\n.quad cross_function_owner\n"
        ".quad cross_object_owner\n.quad missing_object\n.quad missing_fn\n";
    MinicCoreTypeContext types = {0};
    MinicCoreType word = {
        .kind = MINIC_CORE_TYPE_INTEGER, .has_layout = true,
        .size = 8U, .alignment = 8U
    };
    MinicCoreTypeId word_id;
    MinicCoreModuleFragment zeros[2] = {
        { .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_count = 8U },
        { .kind = MINIC_CORE_MODULE_FRAGMENT_ZERO, .byte_count = 8U }
    };
    MinicCoreModuleGlobal globals[8] = {
        { .name = "weak_before", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
          .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        { .name = "weak_before", .emittable = true, .storage_size = 8U,
          .storage_alignment = 8U, .fragments = &zeros[0], .fragment_count = 1U,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        { .name = "weak_after", .emittable = true, .storage_size = 8U,
          .storage_alignment = 8U, .fragments = &zeros[1], .fragment_count = 1U,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        { .name = "weak_after", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
          .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        { .name = "missing_object", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
          .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        { .name = "unused_object", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
          .is_weak = true,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        /* An object-typed weak declaration has a FUNCTION definition owner. */
        { .name = "cross_function_owner",
          .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
          .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX },
        /* In the opposite order a strong OBJECT precedes a weak function. */
        { .name = "cross_object_owner", .emittable = true,
          .storage_size = 8U, .storage_alignment = 8U,
          .fragments = &zeros[0], .fragment_count = 1U,
          .alias_target_index = SIZE_MAX, .owner_function_index = SIZE_MAX }
    };
    MinicCoreModuleFunctionSymbol symbols[8] = {
        { .name = "weak_fn", .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "weak_fn", .is_defined = true, .is_referenced = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "weak_after_fn", .is_defined = true, .is_referenced = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "weak_after_fn", .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "missing_fn", .is_weak = true, .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "unused_fn", .is_weak = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "cross_function_owner", .is_defined = true,
          .is_referenced = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX },
        { .name = "cross_object_owner", .is_weak = true,
          .is_file_asm_root = true,
          .alias_target_index = SIZE_MAX, .storage_owner_function_index = SIZE_MAX }
    };
    MinicCoreBlock entry = {
        .has_terminator = true,
        .terminator = { .kind = MINIC_CORE_TERMINATOR_RETURN,
                        .return_value = MINIC_CORE_VALUE_INVALID }
    };
    MinicCoreFunction functions[8] = {0};
    MinicCoreModuleFileAsm file_asm = {
        .text = (char *)file_asm_text, .length = sizeof(file_asm_text) - 1U
    };
    MinicCoreModule module = {
        .type_context = &types, .globals = globals,
        .global_count = sizeof(globals) / sizeof(globals[0]),
        .function_symbols = symbols,
        .function_count = sizeof(symbols) / sizeof(symbols[0]),
        .file_asms = &file_asm, .file_asm_count = 1U,
        .finalized = true
    };
    MinicProgramEmitConfig config = {
        .emit_function = emit_function, .between = emit_file_asm,
        .selection = MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS
    };
    MinicTargetInfo target = {0};
    size_t index;
    if (argc != 3) return 2;
    minic_core_type_context_initialize(&types);
    if (!minic_core_type_register(&types, &word, &word_id)) return 3;
    for (index = 0U; index < 8U; ++index)
        globals[index].core_type_id = word_id;
    functions[1].name = "weak_fn";
    functions[1].type_context = &types;
    functions[1].blocks = &entry;
    functions[1].block_count = 1U;
    functions[2].name = "weak_after_fn";
    functions[2].type_context = &types;
    functions[2].blocks = &entry;
    functions[2].block_count = 1U;
    functions[6].name = "cross_function_owner";
    functions[6].type_context = &types;
    functions[6].blocks = &entry;
    functions[6].block_count = 1U;
    if (!minic_core_module_verify(&module) ||
        !minic_program_emit_write_module(argv[1], &module, &target,
            functions, 8U, false, &config, NULL, emit_object))
        return 4;
    if (!check_direct_tls(argv[2], &types, word_id)) return 5;
    (void)remove(argv[2]);
    minic_core_type_context_destroy(&types);
    puts("MINIC_ELF_SYMBOL_OWNER_TLS=PASS same_kind_owner=4 cross_kind_owner=2 undefined_weak=2 direct_TLS_reject=2 direct_nonTLS_emit=2");
    return 0;
}

/* No CALLs or stack objects are in this standalone Core fixture. */
bool minic_core_object_storage_plan_build(
    const MinicCoreFunction *function, MinicCoreObjectStoragePlan *plan) {
    return function != NULL && plan != NULL && function->object_count == 0U;
}
void minic_core_object_storage_plan_destroy(MinicCoreObjectStoragePlan *plan) {
    if (plan != NULL) *plan = (MinicCoreObjectStoragePlan){0};
}
bool minic_core_call_index_build(const MinicCoreFunction *function,
                                 size_t expected, MinicCoreCallIndex *index) {
    return function != NULL && expected == 0U && index != NULL;
}
void minic_core_call_index_destroy(MinicCoreCallIndex *index) {
    if (index != NULL) *index = (MinicCoreCallIndex){0};
}
bool minic_core_function_verify(const MinicCoreFunction *function) {
    return function != NULL && function->type_context != NULL &&
           function->block_count == 1U && function->blocks != NULL;
}
bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {
    (void)output; (void)function;
    return false;
}
