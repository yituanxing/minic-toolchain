/* A finalized Module and an individually valid Core function must originate
 * from the same type arena before any machine writer can observe their IDs.
 * A merely matching type index is not sufficient: IDs are arena-local. */
#define _POSIX_C_SOURCE 200809L

#include "core/core_ir.h"
#include "core/module_ir.h"
#include "target/backend.h"
#include "target/target_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "BACKEND_MODULE_ARENA=FAIL line=%d expr=%s\n", \
                      __LINE__, #expr); \
        result = 1; \
        goto cleanup; \
    } \
} while (0)

static bool register_void_type(MinicCoreTypeContext *context,
                               MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_VOID;
    return minic_core_type_register(context, &type, id);
}

static void simulated_source_draft_release(MinicCoreSourceDraft *draft) {
    (void)draft;
}

static bool create_void_return_function(MinicCoreFunction *function,
                                        MinicCoreTypeContext *context,
                                        MinicCoreTypeId void_id) {
    MinicCoreBlockId entry;
    MinicCoreTerminator terminator = {0};
    minic_core_function_initialize(function);
    function->name = strdup("arena_probe");
    if (function->name == NULL) return false;
    function->name_length = 11U;
    function->core_return_type_id = void_id;
    if (!minic_core_function_add_block(function, &entry)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, entry, &terminator) &&
           minic_core_function_attach_type_context(function, context) &&
           minic_core_function_verify(function);
}

int main(void) {
    MinicCoreTypeContext good_types, alien_types;
    MinicCoreTypeId good_void = MINIC_CORE_TYPE_INVALID;
    MinicCoreTypeId alien_integer = MINIC_CORE_TYPE_INVALID;
    MinicCoreTypeId alien_void = MINIC_CORE_TYPE_INVALID;
    MinicCoreType integer = {0};
    MinicCoreFunction function;
    MinicCoreModule module;
    MinicCoreFunction absent_function = {0};
    const MinicTargetInfo *target = minic_default_target_info();
    const MinicBackend *backend = minic_backend_for_target(target);
    char path[] = "/tmp/minic-core-arena-XXXXXX";
    int fd = -1;
    int result = 0;

    minic_core_type_context_initialize(&good_types);
    minic_core_type_context_initialize(&alien_types);
    minic_core_function_initialize(&function);
    minic_core_module_initialize(&module);
    REQUIRE(target != NULL && backend != NULL);
    REQUIRE(register_void_type(&good_types, &good_void));
    integer.kind = MINIC_CORE_TYPE_INTEGER;
    integer.has_layout = true;
    integer.size = 4U;
    integer.alignment = 4U;
    integer.value.integer.bit_width = 32U;
    integer.value.integer.is_signed = true;
    REQUIRE(minic_core_type_register(&alien_types, &integer, &alien_integer));
    REQUIRE(register_void_type(&alien_types, &alien_void));
    REQUIRE(good_void == alien_integer && good_void != alien_void);
    REQUIRE(create_void_return_function(&function, &good_types, good_void));

    module.type_context = &good_types;
    module.function_count = 1U;
    module.function_symbols = calloc(1U, sizeof(*module.function_symbols));
    REQUIRE(module.function_symbols != NULL);
    module.function_symbols[0].name = strdup("arena_probe");
    REQUIRE(module.function_symbols[0].name != NULL);
    module.function_symbols[0].alias_target_index = SIZE_MAX;
    module.function_symbols[0].is_defined = true;
    module.finalized = true;
    REQUIRE(minic_core_module_verify(&module));

    fd = mkstemp(path);
    REQUIRE(fd >= 0 && close(fd) == 0);
    fd = -1;
    REQUIRE(unlink(path) == 0);
    /* Positive: an independently constructed Core function and Module share
     * one live type arena and can be emitted without C AST or source drafts. */
    REQUIRE(minic_backend_emit_assembly_with_module(
        backend, path, target, &function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) == 0 && unlink(path) == 0);

    /* Negative: two individually valid arenas have different type ID maps.
     * The backend must reject the mismatch before creating the output. */
    module.type_context = &alien_types;
    REQUIRE(minic_core_module_verify(&module));
    REQUIRE(!minic_backend_emit_assembly_with_module(
        backend, path, target, &function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) != 0);
    module.type_context = &good_types;
    function.type_context = &alien_types;
    REQUIRE(!minic_backend_emit_assembly_with_module(
        backend, path, target, &function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) != 0);
    function.type_context = &good_types;
    REQUIRE(!minic_backend_emit_assembly_with_module(
        backend, path, target, NULL, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) != 0);

    /* Source adapters are only temporary during lowering, not publishable. */
    function.source_draft = (MinicCoreSourceDraft *)&absent_function;
    function.source_draft_release = simulated_source_draft_release;
    REQUIRE(minic_core_function_verify(&function));
    REQUIRE(!minic_backend_emit_assembly_with_module(
        backend, path, target, &function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) != 0);
    function.source_draft = NULL;
    function.source_draft_release = NULL;

    /* Unlowered declaration or unreachable static slots are legal. */
    module.function_symbols[0].is_defined = false;
    REQUIRE(minic_core_module_verify(&module));
    REQUIRE(minic_backend_emit_assembly_with_module(
        backend, path, target, &absent_function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) == 0 && unlink(path) == 0);
    module.function_symbols[0].is_defined = true;
    module.function_symbols[0].is_internal = true;
    module.function_symbols[0].is_referenced = false;
    REQUIRE(minic_backend_emit_assembly_with_module(
        backend, path, target, &absent_function, 1U, false, &module, NULL));
    REQUIRE(access(path, F_OK) == 0 && unlink(path) == 0);

cleanup:
    if (fd >= 0) (void)close(fd);
    (void)unlink(path);
    minic_core_module_destroy(&module);
    minic_core_function_destroy(&function);
    minic_core_type_context_destroy(&alien_types);
    minic_core_type_context_destroy(&good_types);
    if (result != 0) return result;
    (void)puts("BACKEND_MODULE_ARENA=PASS shared_arena mismatched_arena "
               "null_function unpublished_draft declarations skipped_static");
    return 0;
}
