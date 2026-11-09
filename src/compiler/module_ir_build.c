#include "compiler/module_ir_build.h"
#include "core/symbol_identity.h"

#include "compiler/global_payload.h"
#include "target/data_layout_source.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicModuleFragmentSinkContext {
    MinicCoreModuleGlobal *global;
    const MinicC0Program *program;
    const MinicTargetInvocation *target;
    size_t cursor;
} MinicModuleFragmentSinkContext;

/* File-level GNU ASM is always emitted. Capture only its assembler-token
 * references in the sealed Module; function-body ASM roots instead belong to
 * reachable Core ASM instructions, never an all-Source declaration scan. */
static void module_capture_file_asm_roots(MinicCoreModule *module) {
    size_t symbol_index, asm_index;
    for (symbol_index = 0U; symbol_index < module->global_count; ++symbol_index) {
        MinicCoreModuleGlobal *symbol = &module->globals[symbol_index];
        for (asm_index = 0U; asm_index < module->file_asm_count; ++asm_index)
            if (minic_core_asm_names_symbol(module->file_asms[asm_index].text,
                    module->file_asms[asm_index].length, symbol->name)) {
                symbol->is_file_asm_root = true;
                break;
            }
    }
    for (symbol_index = 0U; symbol_index < module->function_count; ++symbol_index) {
        MinicCoreModuleFunctionSymbol *symbol = &module->function_symbols[symbol_index];
        for (asm_index = 0U; asm_index < module->file_asm_count; ++asm_index)
            if (minic_core_asm_names_symbol(module->file_asms[asm_index].text,
                    module->file_asms[asm_index].length, symbol->name)) {
                symbol->is_file_asm_root = true;
                break;
            }
    }
}

static char *module_owned_string(const char *text, size_t length) {
    char *copy;
    if (text == NULL || length == SIZE_MAX) return NULL;
    copy = malloc(length + 1U);
    if (copy == NULL) return NULL;
    if (length != 0U) (void)memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *module_c_string(const char *text) {
    return text != NULL ? module_owned_string(text, strlen(text)) : NULL;
}

static bool module_visibility(MinicSymbolVisibility source,
                              MinicCoreModuleVisibility *output) {
    if (output == NULL) return false;
    switch (source) {
    case MINIC_SYMBOL_VISIBILITY_DEFAULT:
        *output = MINIC_CORE_MODULE_VISIBILITY_DEFAULT; return true;
    case MINIC_SYMBOL_VISIBILITY_HIDDEN:
        *output = MINIC_CORE_MODULE_VISIBILITY_HIDDEN; return true;
    case MINIC_SYMBOL_VISIBILITY_INTERNAL:
        *output = MINIC_CORE_MODULE_VISIBILITY_INTERNAL; return true;
    case MINIC_SYMBOL_VISIBILITY_PROTECTED:
        *output = MINIC_CORE_MODULE_VISIBILITY_PROTECTED; return true;
    }
    return false;
}

static MinicCoreModuleFragment *module_push_fragment(
    MinicModuleFragmentSinkContext *sink, size_t bytes,
    MinicCoreModuleFragmentKind kind) {
    MinicCoreModuleGlobal *global;
    MinicCoreModuleFragment *fragment;
    size_t capacity;
    void *next;
    if (sink == NULL || (global = sink->global) == NULL ||
        bytes == 0U || sink->cursor > global->storage_size ||
        bytes > global->storage_size - sink->cursor)
        return NULL;
    if (global->fragment_count == global->fragment_capacity) {
        capacity = global->fragment_capacity == 0U
            ? 16U : global->fragment_capacity * 2U;
        if (capacity < global->fragment_capacity ||
            capacity > SIZE_MAX / sizeof(*global->fragments)) return NULL;
        next = realloc(global->fragments, capacity * sizeof(*global->fragments));
        if (next == NULL) return NULL;
        global->fragments = next;
        global->fragment_capacity = capacity;
    }
    fragment = &global->fragments[global->fragment_count++];
    (void)memset(fragment, 0, sizeof(*fragment));
    fragment->kind = kind;
    fragment->byte_offset = sink->cursor;
    fragment->byte_count = bytes;
    sink->cursor += bytes;
    return fragment;
}

static bool module_sink_zero(void *context, size_t bytes) {
    if (bytes == 0U) return true;
    /* Preserve every semantic DataLayout segment boundary. In particular,
     * independent 16-byte zeroed slots remain separate .zero directives. */
    return module_push_fragment(context, bytes,
                 MINIC_CORE_MODULE_FRAGMENT_ZERO) != NULL;
}

static bool module_scalar_width_valid(size_t width) {
    return width == 1U || width == 2U || width == 4U || width == 8U;
}

static bool module_sink_bits(void *context, MinicType source_type,
                             size_t width, uint64_t bits) {
    MinicCoreModuleFragment *fragment;
    /* Core owns physical bytes, never the C signedness used to spell them.
     * GNU decimal directive formatting is a target-serializer decision. */
    (void)source_type;
    if (!module_scalar_width_valid(width)) return false;
    fragment = module_push_fragment(context, width, MINIC_CORE_MODULE_FRAGMENT_BITS);
    if (fragment == NULL) return false;
    fragment->bits = bits;
    return true;
}

static bool module_sink_relocation(void *context,
                                  const MinicGlobalRelocation *source,
                                  size_t width) {
    MinicModuleFragmentSinkContext *sink = context;
    MinicCoreModuleFragment *fragment;
    MinicCoreModuleRelocationKind relocation_kind;
    char *owned_symbol = NULL;
    const char *source_symbol;
    int64_t addend = 0;
    if (sink == NULL || source == NULL || !module_scalar_width_valid(width))
        return false;
    /* Resolve and own the entire symbol/relocation proof before committing
     * a fragment. A rejected source relocation must never advance the byte
     * cursor or leave a partially initialized Core slot in the sink. */
    if (source->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *label = minic_c0_program_statement(
            sink->program, source->target_id);
        if (label == NULL || label->kind != MINIC_STATEMENT_LABEL) return false;
        relocation_kind = MINIC_CORE_MODULE_RELOCATION_LABEL;
        if (!minic_core_format_label_symbol(source->target_id, &owned_symbol))
            return false;
    } else if (source->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT ||
               source->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        source_symbol = minic_multi_global_relocation_target_name(sink->program, source);
        if (source_symbol == NULL || source_symbol[0] == '\0' ||
            !minic_data_layout_global_relocation_target_addend(
                sink->target, sink->program, source, &addend))
            return false;
        owned_symbol = module_c_string(source_symbol);
        if (owned_symbol == NULL) return false;
        relocation_kind = source->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT
            ? MINIC_CORE_MODULE_RELOCATION_OBJECT
            : MINIC_CORE_MODULE_RELOCATION_FUNCTION;
    } else {
        return false;
    }
    fragment = module_push_fragment(sink, width,
                                    MINIC_CORE_MODULE_FRAGMENT_RELOCATION);
    if (fragment == NULL) {
        free(owned_symbol);
        return false;
    }
    fragment->relocation_symbol = owned_symbol;
    fragment->relocation_addend = addend;
    fragment->relocation_kind = relocation_kind;
    return true;
}

static bool module_capture_global_identity(MinicCoreModuleGlobal *destination,
                                           const MinicC0Program *program,
                                           const MinicGlobalObject *source,
                                           size_t index) {
    const MinicGlobalObject *alias_target;
    size_t reference_index;
    if (source->name == NULL ||
        (destination->name = module_c_string(source->name)) == NULL ||
        !module_visibility(source->visibility, &destination->visibility))
        return false;
    destination->section_name = module_c_string(source->section_name);
    if (source->section_name != NULL && destination->section_name == NULL)
        return false;
    destination->alias_target_index = SIZE_MAX;
    destination->owner_function_index = source->owner_function == MINIC_FUNCTION_INVALID
        ? SIZE_MAX : (size_t)source->owner_function;
    destination->is_internal = source->is_internal;
    destination->is_weak = source->is_weak;
    destination->is_read_only = source->is_read_only;
    /* Resolve the C declaration form once. All downstream consumers see
     * only one mutually exclusive physical data policy. */
    if (source->is_block_scope_extern_only && !source->is_extern)
        return false;
    if (source->is_tentative && source->is_zero_initialized)
        return false;
    if (source->is_tentative && source->is_extern)
        return false;
    destination->definition_kind = source->is_tentative
        ? MINIC_CORE_MODULE_DEFINITION_ZERO_FILL
        : source->is_extern
            ? MINIC_CORE_MODULE_DEFINITION_DECLARATION
            : MINIC_CORE_MODULE_DEFINITION_REGULAR;
    destination->data_policy = source->is_tentative
        ? MINIC_CORE_MODULE_DATA_ZERO_ONLY
        : source->is_zero_initialized
            ? MINIC_CORE_MODULE_DATA_ZERO_NUMERIC
            : MINIC_CORE_MODULE_DATA_MATERIALIZED;
    destination->is_thread_local = source->is_thread_local;
    destination->is_file_scope_reachability_root =
        source->owner_function == MINIC_FUNCTION_INVALID &&
        (!source->is_internal || source->force_emit ||
         source->section_name != NULL ||
         source->alias_target < program->global_object_count ||
         source->is_weak || source->is_thread_local ||
         source->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT);
    destination->emittable = false;
    destination->core_type_id = MINIC_CORE_TYPE_INVALID;
    if (source->alias_target != MINIC_GLOBAL_OBJECT_INVALID) {
        if (!source->is_extern || source->alias_target >= program->global_object_count ||
            source->alias_target == index) return false;
        alias_target = &program->global_objects[source->alias_target];
        if (alias_target->is_extern || alias_target->is_tentative ||
            !minic_c0_object_alias_types_compatible(program, source->type,
                                                      alias_target->type))
            return false;
        destination->alias_target_index = source->alias_target;
    }
    if (source->relocation_count != 0U && source->relocations == NULL)
        return false;
    destination->reference_count = source->relocation_count;
    if (destination->reference_count != 0U) {
        if (destination->reference_count > SIZE_MAX / sizeof(*destination->references))
            return false;
        destination->references = calloc(destination->reference_count,
                                         sizeof(*destination->references));
        if (destination->references == NULL) return false;
    }
    for (reference_index = 0U; reference_index < destination->reference_count;
         ++reference_index) {
        const MinicGlobalRelocation *relocation = &source->relocations[reference_index];
        MinicCoreModuleReference *reference = &destination->references[reference_index];
        if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
            reference->kind = MINIC_CORE_MODULE_REFERENCE_OBJECT;
            reference->target_index = relocation->target_id;
            if (reference->target_index >= program->global_object_count) return false;
        } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
            reference->kind = MINIC_CORE_MODULE_REFERENCE_FUNCTION;
            reference->target_index = relocation->target_id;
            if (reference->target_index >= program->function_count) return false;
        } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
            /* A local-label relocation names the original FunctionBody's
             * label, never the body of an integer-specialization clone. */
            reference->kind = MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL;
            reference->target_index = destination->owner_function_index;
            if (reference->target_index >= program->function_count) return false;
        } else return false;
    }
    return true;
}

static bool module_capture_global_payload(MinicCoreModuleGlobal *destination,
                                          const MinicC0Program *program,
                                          const MinicTargetInvocation *target,
                                          MinicCoreTypeImporter *importer,
                                          const MinicGlobalObject *source) {
    MinicMultiGlobalEmissionPlan plan;
    MinicModuleFragmentSinkContext sink;
    const MinicMultiGlobalPayloadSink callbacks = {
        &sink, module_sink_zero, module_sink_bits, module_sink_relocation
    };
    if (!destination->emittable) return true;
    if (!minic_core_type_import(importer, source->type,
                                &destination->core_type_id)) return false;
    if (destination->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION)
        return true;
    if (!minic_multi_global_payload_plan_build(program, target, source, &plan))
        return false;
    destination->default_section = plan.default_section;
    destination->storage_size = plan.storage_size;
    destination->storage_alignment = plan.storage_alignment;
    if (source->flexible_array_initializer_count != 0U) {
        const MinicCoreType *declared = minic_core_type_get(
            importer->context, destination->core_type_id);
        size_t extension_bytes;
        /* Source DataLayout has already checked that this is a legal
         * initialized flexible tail and computed its exact total extent.
         * Publish only its physical storage unit, not the C record shape. */
        if (declared == NULL || !declared->has_layout ||
            plan.storage_size <= declared->size)
            return false;
        extension_bytes = plan.storage_size - declared->size;
        if (extension_bytes % source->flexible_array_initializer_count != 0U)
            return false;
        destination->storage_extension_unit_bytes =
            extension_bytes / source->flexible_array_initializer_count;
        if (destination->storage_extension_unit_bytes == 0U) return false;
    }
    sink.global = destination;
    sink.program = program;
    sink.target = target;
    sink.cursor = 0U;
    if (!minic_multi_global_payload_visit(program, target, source, &plan,
                                         &callbacks) ||
        sink.cursor != destination->storage_size)
        return false;
    return true;
}

static bool module_capture_function(MinicCoreModuleFunctionSymbol *destination,
                                    const MinicC0Program *program,
                                    const MinicFunction *source, size_t index) {
    const char *symbol = minic_c0_function_symbol_name(source);
    if (symbol == NULL || (destination->name = module_c_string(symbol)) == NULL ||
        !module_visibility(source->visibility, &destination->visibility))
        return false;
    destination->section_name = module_c_string(source->section_name);
    if (source->section_name != NULL && destination->section_name == NULL)
        return false;
    destination->explicit_alignment = source->explicit_alignment;
    destination->is_defined = source->is_defined;
    destination->is_internal = source->is_internal;
    destination->is_referenced = source->is_referenced;
    destination->is_weak = source->is_weak;
    destination->is_constructor = source->is_constructor;
    /* Resolve source specialization into a language-independent Module
     * storage owner. No source FunctionId meaning survives publication. */
    destination->storage_owner_function_index = source->is_integer_specialization
        ? (size_t)source->specialization_source : SIZE_MAX;
    if (destination->storage_owner_function_index != SIZE_MAX &&
        destination->storage_owner_function_index >= program->function_count)
        return false;
    destination->is_reachability_root =
        !source->is_integer_specialization && source->is_defined &&
        (!source->is_internal || source->force_emit ||
         source->is_constructor || index == program->entry_function);
    /* A C parser reference is not an executable Core root. Historically the
     * first Core pruning pass removed dead internal bodies BEFORE section,
     * assembler-name, visibility and opaque template hints were considered.
     * Let Core edges, alias targets and typed source-ASM operands retain them;
     * otherwise dead if(0) metadata would resurrect unresolved callbacks.
     * In particular a plain inline `cpuid` mnemonic is not a reference to a
     * static function named cpuid. */
    destination->alias_target_index = SIZE_MAX;
    if (source->alias_target != MINIC_FUNCTION_INVALID) {
        const MinicFunction *target;
        if (source->is_defined || source->alias_target >= program->function_count ||
            source->alias_target == index) return false;
        target = &program->functions[source->alias_target];
        if (!target->is_defined || target->alias_target != MINIC_FUNCTION_INVALID)
            return false;
        destination->alias_target_index = source->alias_target;
    }
    return true;
}

bool minic_core_module_prepare_from_program(MinicCoreModule *output,
                                            const MinicC0Program *program,
                                            MinicCoreTypeImporter *importer) {
    MinicCoreModule staged;
    size_t index;
    if (output == NULL || program == NULL || importer == NULL || importer->failed ||
        importer->program != program || importer->context == NULL ||
        program->global_object_count > SIZE_MAX / sizeof(*staged.globals) ||
        program->function_count > SIZE_MAX / sizeof(*staged.function_symbols) ||
        program->file_asm_count > SIZE_MAX / sizeof(*staged.file_asms))
        return false;
    minic_core_module_initialize(&staged);
    staged.type_context = importer->context;
    staged.global_count = program->global_object_count;
    staged.function_count = program->function_count;
    staged.file_asm_count = program->file_asm_count;
    if (staged.global_count != 0U) {
        staged.globals = calloc(staged.global_count, sizeof(*staged.globals));
        if (staged.globals == NULL) goto failed;
    }
    if (staged.function_count != 0U) {
        staged.function_symbols = calloc(staged.function_count,
                                         sizeof(*staged.function_symbols));
        if (staged.function_symbols == NULL) goto failed;
    }
    if (staged.file_asm_count != 0U) {
        staged.file_asms = calloc(staged.file_asm_count, sizeof(*staged.file_asms));
        if (staged.file_asms == NULL) goto failed;
    }
    for (index = 0U; index < staged.global_count; ++index)
        if (!module_capture_global_identity(&staged.globals[index], program,
                                            &program->global_objects[index], index))
            goto failed;
    for (index = 0U; index < staged.function_count; ++index)
        if (!module_capture_function(&staged.function_symbols[index], program,
                                     &program->functions[index], index))
            goto failed;
    for (index = 0U; index < staged.global_count; ++index) {
        const MinicCoreModuleGlobal *global = &staged.globals[index];
        size_t reference_index;
        for (reference_index = 0U; reference_index < global->reference_count;
             ++reference_index) {
            const MinicCoreModuleReference *reference =
                &global->references[reference_index];
            if (reference->kind == MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL)
                staged.function_symbols[reference->target_index].
                    is_owner_label_relocation_target = true;
        }
    }
    for (index = 0U; index < staged.file_asm_count; ++index) {
        const MinicFileAsm *source = &program->file_asms[index];
        if (source->text == NULL || source->length == SIZE_MAX) goto failed;
        staged.file_asms[index].text = module_owned_string(source->text,
                                                            source->length);
        staged.file_asms[index].length = source->length;
        if (staged.file_asms[index].text == NULL) goto failed;
    }
    module_capture_file_asm_roots(&staged);
    minic_core_module_destroy(output);
    *output = staged;
    return true;
failed:
    minic_core_module_destroy(&staged);
    return false;
}

bool minic_core_module_materialize_live_payloads(MinicCoreModule *module,
                                                  const MinicC0Program *program,
                                                  const MinicTargetInvocation *target,
                                                  MinicCoreTypeImporter *importer) {
    size_t index;
    if (module == NULL || program == NULL || target == NULL ||
        target->machine.data_layout == NULL || importer == NULL || importer->failed ||
        importer->program != program || importer->context != module->type_context ||
        module->finalized || module->global_count != program->global_object_count ||
        module->function_count != program->function_count)
        return false;
    /* A dead file-static payload must never be imported. Its C type may be
     * target-unsupported and its initializer is deliberately irrelevant to
     * the emitted object graph. The symbol graph was already closed in Core. */
    for (index = 0U; index < module->global_count; ++index)
        if (!module_capture_global_payload(
                &module->globals[index], program, target,
                importer, &program->global_objects[index])) return false;
    module->finalized = true;
    return minic_core_module_verify(module);
}
