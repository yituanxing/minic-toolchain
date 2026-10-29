#include "frontend/parser_external_object.h"
#include "frontend/attribute.h"
#include "frontend/declaration_sema.h"
#include "frontend/parser_internal.h"
#include "frontend/parser_static_initializer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* File-scope non-function declarations and definitions. */

bool minic_parser_external_object_metadata(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           const char *section_name,
                                           size_t section_name_length,
                                           bool has_section,
                                           size_t explicit_alignment,
                                           MinicSymbolVisibility visibility,
                                           bool has_visibility);

static bool parse_external_integer_array_definition(MinicParser *parser,
                                                    MinicType element_type,
                                                    MinicSourceSpan name_span,
                                                    char *section_name,
                                                    size_t section_name_capacity,
                                                    size_t *section_name_length,
                                                    bool *has_section,
                                                    size_t *explicit_alignment,
                                                    MinicSymbolVisibility visibility,
                                                    bool has_visibility,
                                                    bool *is_weak) {
    MinicGlobalObjectId object_id;
    MinicGlobalObject *object;
    const MinicArrayType *array_type;
    MinicType declared_array_type;
    MinicType declared_element_type;
    size_t element_count;
    bool inferred_bound;
    bool reused_existing;

    if (parser == NULL || section_name == NULL || section_name_length == NULL ||
        has_section == NULL || explicit_alignment == NULL || is_weak == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser,
                           "external array definition requires an integer or pointer element type");
        return false;
    }

    element_count = 0U;
    inferred_bound = false;
    reused_existing = false;
    declared_element_type = element_type;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicType nested_element_type;
        bool nested_is_array;

        nested_is_array = false;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &nested_element_type, &nested_is_array) ||
            !nested_is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build multidimensional external array type");
            }
            return false;
        }
        declared_element_type = nested_element_type;
    }

    if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
            parser,
            section_name,
            section_name_capacity,
            section_name_length,
            has_section,
            explicit_alignment,
            &visibility,
            &has_visibility,
            is_weak)) {
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if ((inferred_bound && !minic_c0_program_add_incomplete_array_type(
                                   parser->program, declared_element_type, &declared_array_type)) ||
            (!inferred_bound &&
             !minic_c0_program_add_array_type(
                 parser->program, declared_element_type, element_count, &declared_array_type)) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                declared_array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external integer array definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        reused_existing = true;
        if ((!object->is_extern && !object->is_tentative) || !minic_type_is_array(object->type)) {
            minic_parser_error(parser, "conflicting external integer array definition");
            return false;
        }
        array_type = minic_c0_program_array_type(parser->program, object->type.array_type_id);
        if (array_type == NULL ||
            !minic_c0_types_compatible(
                parser->program, array_type->element_type, declared_element_type)) {
            minic_parser_error(parser, "external integer array definition type mismatch");
            return false;
        }
        if (array_type->element_count != 0U) {
            if (!inferred_bound && array_type->element_count != element_count) {
                minic_parser_error(parser, "external integer array bound mismatch");
                return false;
            }
            element_count = array_type->element_count;
            inferred_bound = false;
        } else if (!inferred_bound && !minic_c0_program_complete_array_type(
                                          parser->program, object->type, element_count)) {
            minic_parser_error(parser, "cannot complete external integer array bound");
            return false;
        }
    }

    object = &parser->program->global_objects[object_id];
    if ((reused_existing && !minic_c0_global_object_begin_definition(parser->program, object_id)) ||
        !minic_parser_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        *section_name_length,
                                        *has_section,
                                        *explicit_alignment,
                                        visibility,
                                        has_visibility) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    /* Both fixed and inferred aggregate arrays use the same transactional
     * static-initializer engine. A preceding complete [N] declaration can
     * make an omitted-bound [] definition fixed here; both states must still
     * share the common brace path. Keep the brace-free char[] = "..." spelling. */
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!minic_parser_parse_static_storage_initializer_value(
                parser, object_id, object->type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize external aggregate array");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
    }
    if (inferred_bound && minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t string_count;
        if (!minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
            !minic_c0_program_complete_array_type(
                parser->program, object->type, string_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported external string array definition");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
    }
    minic_parser_error(parser, "external array initializer requires braces or a character string");
    return false;
}

bool minic_parser_external_object_metadata(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           const char *section_name,
                                           size_t section_name_length,
                                           bool has_section,
                                           size_t explicit_alignment,
                                           MinicSymbolVisibility visibility,
                                           bool has_visibility) {
    MinicDeclarationExternalObjectAttributes attributes;

    if (parser == NULL || object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        return false;
    }
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = section_name;
    attributes.section_name_length = section_name_length;
    attributes.explicit_alignment = explicit_alignment;
    attributes.visibility = visibility;
    attributes.has_section = has_section;
    attributes.has_visibility = has_visibility;
    if (!minic_declaration_apply_object_attributes(parser->program, object_id, &attributes)) {
        minic_parser_error(parser, "conflicting external object definition attributes");
        return false;
    }
    return true;
}

static bool record_external_tentative_object(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span,
                                             const char *section_name,
                                             size_t section_name_length,
                                             bool has_section,
                                             size_t explicit_alignment,
                                             MinicSymbolVisibility visibility,
                                             bool has_visibility,
                                             MinicGlobalObjectId *object_id_out) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;

    if (parser == NULL || object_id_out == NULL ||
        (!minic_c0_type_is_complete_object(parser->program, object_type) &&
         !minic_type_is_array(object_type))) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "external tentative definition requires a complete object type");
        }
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        MinicType instance_type;

        if (!minic_declaration_instantiate_incomplete_array_object_type(
                parser->program, object_type, &instance_type)) {
            minic_parser_error(parser, "cannot instantiate external tentative array type");
            return false;
        }
        if (!minic_c0_program_add_tentative_global_object(parser->program,
                                                          parser->source + name_span.begin.offset,
                                                          minic_parser_span_length(name_span),
                                                          instance_type,
                                                          false,
                                                          minic_type_is_const(object_type),
                                                          &object_id)) {
            minic_parser_error(parser, "cannot create external tentative definition");
            return false;
        }
    } else {
        bool discard_declared_array;

        existing = minic_c0_program_global_object(parser->program, object_id);
        discard_declared_array =
            existing != NULL && minic_type_is_array(existing->type) &&
            minic_type_is_array(object_type) &&
            existing->type.array_type_id != object_type.array_type_id &&
            object_type.array_type_id + 1U == parser->program->array_type_count;
        /* TLS linkage is part of the object declaration's ABI contract.
         * A plain tentative definition must never silently coalesce with
         * an earlier opaque extern __thread symbol. */
        if (existing != NULL && existing->is_thread_local) {
            minic_parser_error(parser,
                "conflicting TLS qualification on tentative object definition");
            return false;
        }
        if (existing == NULL ||
            !minic_declaration_external_object_types_compatible(
                parser->program, existing->type, object_type) ||
            (minic_type_is_array(existing->type) &&
             !minic_declaration_merge_external_array_composite_type(
                 parser->program, existing->type, object_type)) ||
            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
            minic_parser_error(parser, "conflicting external tentative definition");
            return false;
        }
        /* The declaration descriptor is only a parser-side candidate once a canonical
         * external entity already exists. Keep the canonical descriptor and retire a
         * newly-created outer array descriptor after its composite information has been
         * merged, otherwise an incomplete `name[]` redeclaration survives ownerless in
         * the Program array arena and correctly trips the AST verifier. */
        if (discard_declared_array &&
            !minic_c0_program_discard_last_array_type(parser->program, object_type)) {
            minic_parser_error(parser, "cannot retire transient external array declaration");
            return false;
        }
    }
    if (!minic_declaration_mark_file_scope_object(parser->program, object_id)) {
        minic_parser_error(parser, "cannot promote external object to file scope");
        return false;
    }
    if (!minic_parser_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility)) {
        return false;
    }
    *object_id_out = object_id;
    return true;
}

bool minic_parser_external_object_weak(MinicParser *parser, MinicSourceSpan name_span, bool is_weak) {
    MinicGlobalObjectId object_id;

    if (!is_weak) {
        return true;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
        minic_parser_error(parser, "GNU weak requires external object linkage");
        return false;
    }
    return true;
}

bool minic_parser_external_tentative_object(MinicParser *parser,
                                            MinicType object_type,
                                            MinicSourceSpan name_span,
                                            const char *section_name,
                                            size_t section_name_length,
                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
                                            bool has_visibility) {
    MinicGlobalObjectId object_id;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        !record_external_tentative_object(parser,
                                          object_type,
                                          name_span,
                                          section_name,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment,
                                          visibility,
                                          has_visibility,
                                          &object_id)) {
        return false;
    }
    (void)object_id;
    return minic_parser_advance(parser);
}

bool minic_parser_external_tentative_declaration_list_after_head(MinicParser *parser,
                                                     MinicType base_type,
                                                     MinicType first_type,
                                                     MinicSourceSpan first_name,
                                                     const char *first_section_name,
                                                     size_t first_section_name_length,
                                                     bool first_has_section,
                                                     size_t first_explicit_alignment,
                                                     MinicSymbolVisibility first_visibility,
                                                     bool first_has_visibility) {
    MinicGlobalObjectId object_id;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_COMMA ||
        !record_external_tentative_object(parser,
                                          first_type,
                                          first_name,
                                          first_section_name,
                                          first_section_name_length,
                                          first_has_section,
                                          first_explicit_alignment,
                                          first_visibility,
                                          first_has_visibility,
                                          &object_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicSourceSpan name_span;
        MinicType object_type;
        char section_name[256];
        size_t section_name_length;
        size_t explicit_alignment;
        MinicSymbolVisibility visibility;
        bool has_section;
        bool has_visibility;
        bool is_array;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        object_type = base_type;
        if (!minic_parser_parse_pointer_declarator(parser, object_type, &object_type) ||
            !minic_parser_parse_direct_declarator_name(parser, &name_span) ||
            !minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array)) {
            return false;
        }
        (void)is_array;
        (void)memset(section_name, 0, sizeof(section_name));
        section_name_length = 0U;
        explicit_alignment = 0U;
        visibility = first_visibility;
        has_section = false;
        has_visibility = first_has_visibility;
        if (!minic_parser_parse_gnu_object_attribute_lists_with_visibility(parser,
                                                                           section_name,
                                                                           sizeof(section_name),
                                                                           &section_name_length,
                                                                           &has_section,
                                                                           &explicit_alignment,
                                                                           &visibility,
                                                                           &has_visibility)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            minic_parser_error(parser,
                               "mixed initialized external declarator lists are not supported yet");
            return false;
        }
        if (!record_external_tentative_object(parser,
                                              object_type,
                                              name_span,
                                              section_name,
                                              section_name_length,
                                              has_section,
                                              explicit_alignment,
                                              visibility,
                                              has_visibility,
                                              &object_id)) {
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object declaration list");
}

bool minic_parser_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span,
                                             const char *section_name,
                                             size_t section_name_length,
                                             bool has_section,
                                             size_t explicit_alignment,
                                             MinicSymbolVisibility visibility,
                                             bool has_visibility) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;
    MinicType declared_object_type;

    declared_object_type = object_type;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type) && !minic_type_is_array(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_declaration_instantiate_incomplete_array_object_type(
                parser->program, object_type, &object_type)) {
            minic_parser_error(parser, "cannot instantiate external array definition type");
            return false;
        }
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        bool discard_declared_array;
        bool types_compatible;
        bool composite_merged;
        bool definition_begun;

        existing = minic_c0_program_global_object(parser->program, object_id);
        discard_declared_array =
            existing != NULL && minic_type_is_array(existing->type) &&
            minic_type_is_array(object_type) &&
            existing->type.array_type_id != object_type.array_type_id &&
            object_type.array_type_id + 1U == parser->program->array_type_count;
        types_compatible =
            existing != NULL &&
            minic_declaration_external_object_types_compatible(
                parser->program, existing->type, object_type);
        composite_merged =
            types_compatible &&
            (!minic_type_is_array(existing->type) ||
             minic_declaration_merge_external_array_composite_type(
                 parser->program, existing->type, object_type));
        definition_begun =
            composite_merged &&
            minic_c0_global_object_begin_definition(parser->program, object_id);
        if (!definition_begun) {
            const char *trace = getenv("CORE_FAST_TRACE");
            if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
                const MinicArrayType *existing_array =
                    existing != NULL && minic_type_is_array(existing->type)
                        ? minic_c0_program_array_type(
                              parser->program, existing->type.array_type_id)
                        : NULL;
                const MinicArrayType *declared_array =
                    minic_type_is_array(object_type)
                        ? minic_c0_program_array_type(
                              parser->program, object_type.array_type_id)
                        : NULL;
                (void)fprintf(stderr,
                              "EXTERN_DEF_DETAIL name=%.*s compat=%d merge=%d begin=%d "
                              "existing_extern=%d tentative=%d internal=%d zero=%d "
                              "existing_init=%zu existing_reloc=%zu existing_array=%d "
                              "existing_count=%zu declared_array=%d declared_count=%zu\n",
                              existing != NULL ? (int)existing->name_length : 0,
                              existing != NULL ? existing->name : "",
                              types_compatible ? 1 : 0,
                              composite_merged ? 1 : 0,
                              definition_begun ? 1 : 0,
                              existing != NULL && existing->is_extern ? 1 : 0,
                              existing != NULL && existing->is_tentative ? 1 : 0,
                              existing != NULL && existing->is_internal ? 1 : 0,
                              existing != NULL && existing->is_zero_initialized ? 1 : 0,
                              existing != NULL ? existing->initializer_count : 0U,
                              existing != NULL ? existing->relocation_count : 0U,
                              existing_array != NULL ? 1 : 0,
                              existing_array != NULL ? existing_array->element_count : 0U,
                              declared_array != NULL ? 1 : 0,
                              declared_array != NULL ? declared_array->element_count : 0U);
            }
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
        if (discard_declared_array) {
            object_type = parser->program->global_objects[object_id].type;
            if (!minic_c0_program_discard_last_array_type(parser->program, declared_object_type)) {
                minic_parser_error(parser, "cannot retire transient external array definition");
                return false;
            }
        }
    }
    if (!minic_parser_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility) ||
        !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(object_type)) {
        if (!minic_parser_parse_static_integer_object_initializer(
                parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }
    if (minic_type_is_record(object_type) || minic_type_is_array(object_type)) {
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record external record initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(object_type)) {
        if (!minic_parser_parse_static_pointer_object_initializer(parser, object_id, object_type)) {
            return false;
        }
    } else {
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
}

static bool parse_inferred_external_record_array_definition(MinicParser *parser,
                                                            MinicType element_type,
                                                            MinicSourceSpan name_span,
                                                            char *section_name,
                                                            size_t section_name_capacity,
                                                            size_t *section_name_length,
                                                            bool *has_section,
                                                            size_t *explicit_alignment,
                                                            MinicSymbolVisibility visibility,
                                                            bool has_visibility) {
    const MinicRecord *record;
    MinicParser initializer_probe;
    MinicType array_type;
    size_t element_count;

    if (parser == NULL || section_name == NULL || section_name_length == NULL ||
        has_section == NULL || explicit_alignment == NULL || !minic_type_is_record(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(
            parser, "inferred external record array requires a complete record element type");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred external record array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_name_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "incomplete external tentative array is not implemented yet");
        return false;
    }
    {
        MinicGlobalObjectId existing_id;
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        existing_id = minic_parser_find_global_object_entity(parser, name_span);
        existing = existing_id == MINIC_GLOBAL_OBJECT_INVALID
                       ? NULL
                       : minic_c0_program_global_object(parser->program, existing_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;

        /* A definition with an omitted bound composes with an earlier visible
           complete array declaration.  Preserve the established bound and use
           the initializer only to populate it; infer a new bound only when no
           complete composite type exists yet. */
        if (existing_array != NULL && !existing_array->is_zero_length &&
            existing_array->element_count != 0U &&
            minic_declaration_external_object_types_compatible(
                parser->program, existing_array->element_type, element_type)) {
            array_type = existing->type;
        } else {
            initializer_probe = *parser;
            if (!minic_parser_advance(&initializer_probe) ||
                !minic_parser_inspect_array_initializer_extent(
                    &initializer_probe, &element_count) ||
                !minic_c0_program_add_array_type(
                    parser->program, element_type, element_count, &array_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "cannot complete inferred external record array type");
                }
                return false;
            }
        }
    }
    return minic_parser_external_object_definition(parser,
                                            array_type,
                                            name_span,
                                            section_name,
                                            *section_name_length,
                                            *has_section,
                                            *explicit_alignment,
                                            visibility,
                                            has_visibility);
}

static bool incomplete_array_declarator_is_tentative(const MinicParser *parser) {
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RBRACKET ||
        !minic_parser_advance(&probe)) {
        return false;
    }
    while (minic_parser_current_identifier_lexeme_equals_cstr(&probe, "__attribute__") ||
           minic_parser_current_identifier_lexeme_equals_cstr(&probe, "__attribute")) {
        size_t depth;

        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN) {
            return false;
        }
        depth = 0U;
        for (;;) {
            if (probe.current.kind == MINIC_TOKEN_LPAREN) {
                depth += 1U;
            } else if (probe.current.kind == MINIC_TOKEN_RPAREN) {
                if (depth == 0U) {
                    return false;
                }
                depth -= 1U;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (depth == 0U) {
                break;
            }
        }
    }
    return probe.current.kind == MINIC_TOKEN_SEMICOLON;
}

bool minic_parser_external_visible_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         char *section_name,
                                         size_t section_name_capacity,
                                         size_t *section_name_length,
                                         bool *has_section,
                                         size_t *explicit_alignment,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility,
                                         bool *is_weak) {
    MinicParser probe;
    MinicType array_type;
    bool is_array;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        is_weak == NULL) {
        return false;
    }

    if (incomplete_array_declarator_is_tentative(parser)) {
        MinicGlobalObjectId existing_id;
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        existing_id = minic_parser_find_global_object_entity(parser, name_span);
        existing = existing_id == MINIC_GLOBAL_OBJECT_INVALID
                       ? NULL
                       : minic_c0_program_global_object(parser->program, existing_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        if (existing_array != NULL &&
            minic_declaration_external_object_types_compatible(
                parser->program, existing_array->element_type, element_type)) {
            if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['") ||
                !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
                return false;
            }
            array_type = existing->type;
            is_array = true;
        } else if (!minic_parser_parse_array_declarator_suffix(
                       parser, element_type, true, &array_type, &is_array) ||
                   !is_array || !minic_type_is_array(array_type)) {
            return false;
        }
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                section_name_capacity,
                section_name_length,
                has_section,
                explicit_alignment,
                &visibility,
                &has_visibility,
                is_weak)) {
            return false;
        }
        return minic_parser_external_tentative_object(parser,
                                               array_type,
                                               name_span,
                                               section_name,
                                               *section_name_length,
                                               *has_section,
                                               *explicit_alignment,
                                               visibility,
                                               has_visibility);
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (minic_type_is_record(element_type)) {
            return parse_inferred_external_record_array_definition(parser,
                                                                   element_type,
                                                                   name_span,
                                                                   section_name,
                                                                   section_name_capacity,
                                                                   section_name_length,
                                                                   has_section,
                                                                   explicit_alignment,
                                                                   visibility,
                                                                   has_visibility);
        }
        return parse_external_integer_array_definition(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       section_name_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment,
                                                       visibility,
                                                       has_visibility,
                                                       is_weak);
    }

    if (!minic_parser_parse_array_declarator_suffix(
            parser, element_type, true, &array_type, &is_array) ||
        !is_array ||
        !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(parser,
                                                                            section_name,
                                                                            section_name_capacity,
                                                                            section_name_length,
                                                                            has_section,
                                                                            explicit_alignment,
                                                                            &visibility,
                                                                            &has_visibility,
                                                                            is_weak)) {
        return false;
    }
    /* A declarator list can start with an array and continue with
     * pointers or scalars: SValue stack[N], *top, *previous.
     * Reuse the existing generic tentative declarator-list handler
     * instead of misclassifying the comma as an initializer. */
    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        return minic_parser_external_tentative_declaration_list_after_head(
            parser, element_type, array_type, name_span, section_name,
            *section_name_length, *has_section, *explicit_alignment,
            visibility, has_visibility);
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_external_tentative_object(parser,
                                               array_type,
                                               name_span,
                                               section_name,
                                               *section_name_length,
                                               *has_section,
                                               *explicit_alignment,
                                               visibility,
                                               has_visibility);
    }
    return minic_parser_external_object_definition(parser,
                                            array_type,
                                            name_span,
                                            section_name,
                                            *section_name_length,
                                            *has_section,
                                            *explicit_alignment,
                                            visibility,
                                            has_visibility);
}
