#include "compiler/module_ir_build.h"
#include "compiler/inline_specialize_internal.h"
#include "core/symbol_identity.h"

#include "compiler/global_payload.h"
#include "frontend/ast_traversal.h"
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

typedef struct MinicModuleAsmRootWalk {
    const MinicC0Program *program;
    MinicCoreModule *module;
    unsigned int depth;
} MinicModuleAsmRootWalk;

/* All source GNU asm operand expressions, including an unreachable asm,
 * may become symbolic immediates without a Core address instruction.  Capture
 * typed function/object roots once; never ask Core closure to parse C syntax
 * or architecture-dependent template text. An opaque nested expression
 * conservatively disables object pruning, matching the historical contract. */
static bool module_asm_operand_root_visit(MinicExpressionId *id_opaque,
                                          void *context_opaque) {
    MinicModuleAsmRootWalk *walk = context_opaque;
    const MinicExpression *expression;
    MinicExpression expression_view;
    bool ok;
    if (walk == NULL || id_opaque == NULL || walk->program == NULL ||
        walk->module == NULL || walk->depth >= 64U ||
        *id_opaque >= walk->program->expression_count) return false;
    expression = minic_c0_program_expression(walk->program, *id_opaque);
    if (expression == NULL) return false;
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        size_t id = expression->value.global_object_id;
        if (id >= walk->module->global_count) return false;
        walk->module->globals[id].is_source_asm_operand_root = true;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        size_t id = expression->value.function_id;
        if (id >= walk->module->function_count) return false;
        walk->module->function_symbols[id].is_source_asm_operand_root = true;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL ||
        expression->kind == MINIC_EXPRESSION_STATEMENT) return false;
    expression_view = *expression;
    ++walk->depth;
    ok = minic_c0_expression_visit_child_id_refs(
        &expression_view, module_asm_operand_root_visit, walk);
    --walk->depth;
    return ok;
}

static void module_capture_asm_operand_roots(MinicCoreModule *module,
                                             const MinicC0Program *program) {
    size_t i;
    for (i = 0U; i < program->inline_asm_count; ++i) {
        const MinicInlineAsm *source = &program->inline_asms[i];
        MinicModuleAsmRootWalk walk = {program, module, 0U};
        size_t j;
        if ((source->input_count != 0U && source->inputs == NULL) ||
            (source->output_count != 0U && source->outputs == NULL)) {
            module->preserve_all_static_objects = true;
            return;
        }
        for (j = 0U; j < source->input_count; ++j) {
            MinicExpressionId id = source->inputs[j].expression;
            if (!module_asm_operand_root_visit(&id, &walk)) {
                module->preserve_all_static_objects = true;
                return;
            }
        }
        for (j = 0U; j < source->output_count; ++j) {
            MinicExpressionId id = source->outputs[j].expression;
            if (!module_asm_operand_root_visit(&id, &walk)) {
                module->preserve_all_static_objects = true;
                return;
            }
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
    if (!module_scalar_width_valid(width)) return false;
    fragment = module_push_fragment(context, width, MINIC_CORE_MODULE_FRAGMENT_BITS);
    if (fragment == NULL) return false;
    fragment->bits = bits;
    fragment->bits_signed_integer = width > 1U &&
        !minic_type_is_float(source_type) &&
        !minic_type_is_double(source_type) &&
        !(minic_type_is_integer(source_type) &&
          minic_type_is_unsigned_integer(source_type));
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
    destination->is_extern = source->is_extern;
    destination->is_internal = source->is_internal;
    destination->is_weak = source->is_weak;
    destination->is_read_only = source->is_read_only;
    destination->is_zero_initialized = source->is_zero_initialized;
    destination->is_tentative = source->is_tentative;
    destination->is_block_scope_extern_only = source->is_block_scope_extern_only;
    destination->is_thread_local = source->is_thread_local;
    destination->explicit_alignment = source->explicit_alignment;
    destination->initializer_slot_count = source->initializer_count;
    destination->relocation_slot_count = source->relocation_count;
    destination->was_source_emission_pruned = source->is_emission_pruned;
    destination->is_file_scope_reachability_root =
        source->owner_function == MINIC_FUNCTION_INVALID &&
        (!source->is_internal || source->force_emit ||
         source->section_name != NULL ||
         source->alias_target < program->global_object_count ||
         source->is_weak || source->is_thread_local ||
         source->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT ||
         minic_program_has_opaque_asm_symbol(
             program, source->name, source->name_length));
    destination->emittable = false;
    destination->is_alias = source->alias_target != MINIC_GLOBAL_OBJECT_INVALID;
    destination->core_type_id = MINIC_CORE_TYPE_INVALID;
    if (destination->is_alias) {
        if (!source->is_extern || source->alias_target >= program->global_object_count ||
            source->alias_target == index) return false;
        alias_target = &program->global_objects[source->alias_target];
        if (alias_target->is_extern || alias_target->is_tentative ||
            !minic_c0_object_alias_types_compatible(program, source->type,
                                                      alias_target->type))
            return false;
        destination->alias_target_index = source->alias_target;
        destination->alias_target_name = module_c_string(alias_target->name);
        if (destination->alias_target_name == NULL) return false;
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
    if (destination->is_extern) return true;
    if (!minic_multi_global_payload_plan_build(program, target, source, &plan))
        return false;
    destination->payload_kind = plan.payload_kind;
    destination->default_section = plan.default_section;
    destination->common_elf_bss_admitted =
        minic_multi_global_explicit_bss_admitted(source);
    destination->storage_size = plan.storage_size;
    destination->storage_alignment = plan.storage_alignment;
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
        (destination->source_name = module_owned_string(
            source->name, source->name_length)) == NULL ||
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
    destination->is_variadic = source->is_variadic;
    destination->is_specialization = source->is_integer_specialization;
    destination->specialization_source_index = source->is_integer_specialization
        ? (size_t)source->specialization_source : SIZE_MAX;
    if (destination->is_specialization &&
        destination->specialization_source_index >= program->function_count)
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
        destination->alias_target_name = module_c_string(
            minic_c0_function_symbol_name(target));
        if (destination->alias_target_name == NULL) return false;
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
                    is_source_label_relocation_owner = true;
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
    module_capture_asm_operand_roots(&staged, program);
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
