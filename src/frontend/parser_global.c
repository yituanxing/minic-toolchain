#include "frontend/parser_internal.h"
#include "frontend/parser_static_initializer.h"
#include "frontend/parser_static_constants.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"
#include "frontend/semantic_snapshot.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool minic_parser_global_apply_metadata(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const char *section_name,
                                         size_t section_name_length,
                                         bool has_section,
                                         size_t explicit_alignment) {
    MinicDeclarationExternalObjectAttributes attributes;

    if (parser == NULL || object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        return false;
    }
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = section_name;
    attributes.section_name_length = section_name_length;
    attributes.explicit_alignment = explicit_alignment;
    attributes.has_section = has_section;
    return minic_declaration_apply_object_attributes(parser->program, object_id, &attributes);
}


bool minic_parser_global_begin_definition(MinicParser *parser,
                                           MinicType type,
                                           MinicSourceSpan name_span,
                                           MinicGlobalObjectId *object_id) {
    const MinicGlobalObject *existing;

    if (parser == NULL || object_id == NULL) {
        return false;
    }
    *object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (*object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                type,
                                                true,
                                                minic_type_is_const(type),
                                                object_id)) {
            minic_parser_error(parser, "cannot create static object definition");
            return false;
        }
        return true;
    }
    existing = minic_c0_program_global_object(parser->program, *object_id);
    if (existing == NULL || !existing->is_internal || !minic_type_equal(existing->type, type) ||
        !minic_c0_global_object_begin_definition(parser->program, *object_id)) {
        minic_parser_error(parser, "conflicting static object definition");
        return false;
    }
    return true;
}

bool minic_parser_parse_static_pointer_object_initializer(MinicParser *parser,
                                                          MinicGlobalObjectId object_id,
                                                          MinicType pointer_type) {
    MinicStaticPointerInitializer initializer;
    bool recorded;

    if (parser == NULL || object_id >= parser->program->global_object_count ||
        !minic_type_is_pointer(pointer_type) ||
        !minic_parser_static_symbol_pointer_initializer(parser, pointer_type, &initializer)) {
        return false;
    }
    if (!initializer.has_relocation) {
        if (!minic_c0_global_object_add_initializer_bits(
                parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
        return true;
    }
    recorded = minic_parser_static_commit_pointer_relocation(
        parser->program, object_id, MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
        0U, &initializer);
    if (!recorded || !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot record static symbolic pointer relocation");
        return false;
    }
    return true;
}

bool minic_parser_global_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span);

static bool parse_static_declarator_tail(MinicParser *parser, MinicType base_type);

static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    bool braced;

    if (!minic_parser_global_begin_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static scalar initializer");
        }
        return false;
    }

    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        if (!minic_parser_parse_static_integer_object_initializer(parser, object_id, type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static integer initializer");
            }
            return false;
        }
    } else if (minic_type_is_float(type) || minic_type_is_double(type)) {
        uint64_t bits;

        if (!minic_parser_static_symbol_floating_initializer_bits(parser, type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static floating initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        if (!minic_parser_parse_static_pointer_object_initializer(parser, object_id, type)) {
            return false;
        }
    } else {
        minic_parser_error(parser, "unsupported static scalar type");
        return false;
    }
    if (braced) {
        if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after static scalar initializer")) {
            return false;
        }
    }
    return parse_static_declarator_tail(parser, type);
}

/* A C declaration may define several objects using one base type, including
 * independently initialized arrays.  The array paths used to require a ';'
 * immediately after their first initializer, even for the valid GNU Linux
 * form `static u8 a[N] = {...}, b[N] = {...};`.  Resume at the next declarator
 * instead of treating the comma as part of the initializer. */
static bool parse_static_declarator_tail(MinicParser *parser, MinicType base_type) {
    MinicSourceSpan name_span;
    MinicType object_type;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    if (parser == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_advance(parser);
    }
    if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser)) {
        minic_parser_error(parser, "expected ',' or ';' after static initializer");
        return false;
    }
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &object_type) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected static declarator after initializer");
        }
        return false;
    }
    name_span = parser->current.span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        return minic_parser_parse_static_zero_declaration_list_after_head(parser,
                                                                           base_type,
                                                                           object_type,
                                                                           name_span,
                                                                           NULL,
                                                                           0U,
                                                                           false,
                                                                           0U);
    }
    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    return minic_parser_parse_static_global_after_head(parser,
                                                       object_type,
                                                       name_span,
                                                       section_name,
                                                       sizeof(section_name),
                                                       &section_name_length,
                                                       &has_section,
                                                       &explicit_alignment);
}

static bool parse_extern_function_pointer_object_declarator(MinicParser *parser,
                                                            MinicType return_type,
                                                            MinicSourceSpan *name_span,
                                                            MinicType *object_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser,
                           "variadic extern function pointer objects are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, object_type)) {
        minic_parser_error(parser, "cannot build extern function pointer object type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

static bool parse_extern_object_declarator(MinicParser *parser,
                                           MinicType base_type,
                                           MinicSourceSpan *name_span,
                                           MinicType *object_type) {
    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_pointer_declarator(parser, base_type, object_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return parse_extern_function_pointer_object_declarator(
            parser, *object_type, name_span, object_type);
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    *name_span = parser->current.span;
    return minic_parser_advance(parser);
}

static bool merge_extern_object_declaration(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            MinicType declared_type,
                                            const char *section_name,
                                            size_t section_name_length,
                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
                                            bool has_visibility) {
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectMergeStatus status;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    attributes.section_name = section_name;
    attributes.section_name_length = section_name_length;
    attributes.explicit_alignment = explicit_alignment;
    attributes.visibility = visibility;
    attributes.has_section = has_section;
    attributes.has_visibility = has_visibility;
    status = minic_declaration_merge_external_object(
        parser->program, object_id, declared_type, &attributes);
    switch (status) {
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK:
        return true;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT:
        minic_parser_error(parser, "conflicting extern object redeclaration");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT:
        minic_parser_error(parser, "conflicting extern object redeclaration attributes");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_INVALID:
        minic_parser_error(parser, "invalid extern object redeclaration");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_COMMIT_FAILED:
    default:
        minic_parser_error(parser, "cannot commit extern object redeclaration");
        return false;
    }
}

/* The first declarator of `extern T (*hooks[])(void);` is built by the
 * function-declarator parser before this declaration-list transaction starts.
 * A duplicate declaration keeps the canonical object type; retire the
 * otherwise ownerless fresh array descriptor just as the suffix parser does
 * for subsequent declarators. A typedef/shared descriptor must survive. */
static bool discard_unowned_first_extern_array_candidate(MinicC0Program *program,
                                                         MinicType declared_type) {
    size_t index;

    if (program == NULL || !minic_type_is_array(declared_type) ||
        declared_type.array_type_id + 1U != program->array_type_count) {
        return true;
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (minic_type_is_array(program->type_aliases[index].type) &&
            program->type_aliases[index].type.array_type_id == declared_type.array_type_id) {
            return true;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        if (minic_type_is_array(program->global_objects[index].type) &&
            program->global_objects[index].type.array_type_id == declared_type.array_type_id) {
            return true;
        }
    }
    return minic_c0_program_discard_last_array_type(program, declared_type);
}

bool minic_parser_declare_block_scope_extern_object(MinicParser *parser,
                                                    MinicSourceSpan name_span,
                                                    MinicType object_type,
                                                    MinicGlobalObjectId *object_id) {
    MinicGlobalObjectId existing_id;

    /* GNU linker section boundaries are often declared `extern const void`
     * inside a function. Such declarations own no storage and only their
     * addresses can be used. Global-object creation and AST verification
     * already reject *definitions* of void objects; keep that distinction. */
    if (parser == NULL || object_id == NULL || minic_type_is_function(object_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid block-scope extern object type");
        }
        return false;
    }
    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        if (!merge_extern_object_declaration(parser,
                                             existing_id,
                                             object_type,
                                             NULL,
                                             0U,
                                             false,
                                             0U,
                                             MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                             false)) {
            return false;
        }
        *object_id = existing_id;
        return true;
    }
    {
        MinicDeclarationExternalObjectAttributes attributes;
        MinicDeclarationExternalObjectCreateStatus create_status;

        (void)memset(&attributes, 0, sizeof(attributes));
        attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
        create_status =
            minic_declaration_create_external_object(parser->program,
                                                     parser->source + name_span.begin.offset,
                                                     minic_parser_span_length(name_span),
                                                     object_type,
                                                     minic_type_is_const(object_type),
                                                     false,
                                                     true,
                                                     &attributes,
                                                     object_id);
        if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
            minic_parser_error(parser, "cannot declare block-scope extern object");
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_extern_global_after_head(MinicParser *parser,
                                                 MinicType base_type,
                                                 MinicType first_object_type,
                                                 MinicSourceSpan first_name_span,
                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 size_t shared_explicit_alignment,
                                                 MinicSymbolVisibility visibility,
                                                 bool has_visibility) {
    bool first_declarator;

    if (parser == NULL) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        char declarator_section_name[256];
        size_t declarator_section_name_length;
        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        MinicSymbolVisibility declarator_visibility;
        bool declarator_has_visibility;
        bool declarator_is_weak;
        MinicGlobalObjectId declarator_alias_target;
        bool object_was_existing;
        bool is_array;
        bool first_declarator_array;
        MinicType declarator_element_type;
        size_t array_type_begin;

        declarator_section_name_length = section_name_length;
        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        declarator_visibility = visibility;
        declarator_has_visibility = has_visibility;
        declarator_is_weak = false;
        declarator_alias_target = MINIC_GLOBAL_OBJECT_INVALID;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
        if (has_section) {
            if (section_name == NULL ||
                section_name_length + 1U > sizeof(declarator_section_name)) {
                minic_parser_error(parser, "invalid shared GNU section attribute");
                return false;
            }
            (void)memcpy(declarator_section_name, section_name, section_name_length + 1U);
        }

        first_declarator_array = first_declarator;
        if (first_declarator) {
            name_span = first_name_span;
            object_type = first_object_type;
            first_declarator = false;
        } else if (!parse_extern_object_declarator(parser, base_type, &name_span, &object_type)) {
            return false;
        }
        declarator_element_type = object_type;
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata_and_alias(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility,
                &declarator_is_weak,
                &declarator_alias_target)) {
            return false;
        }
        if (minic_type_is_function(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        array_type_begin = parser->program->array_type_count;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata_and_alias(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility,
                &declarator_is_weak,
                &declarator_alias_target)) {
            return false;
        }

        object_id = minic_parser_find_global_object_entity(parser, name_span);
        object_was_existing = object_id != MINIC_GLOBAL_OBJECT_INVALID;
        if (object_was_existing) {
            if (!merge_extern_object_declaration(parser,
                                                 object_id,
                                                 object_type,
                                                 declarator_section_name,
                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 declarator_explicit_alignment,
                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
                return false;
            }
            parser->program->array_type_count = array_type_begin;
            if (first_declarator_array &&
                !discard_unowned_first_extern_array_candidate(parser->program, object_type)) {
                minic_parser_error(parser, "cannot retire merged extern array candidate");
                return false;
            }
        } else {
            MinicDeclarationExternalObjectAttributes attributes;
            MinicDeclarationExternalObjectCreateStatus create_status;

            attributes.section_name = declarator_section_name;
            attributes.section_name_length = declarator_section_name_length;
            attributes.explicit_alignment = declarator_explicit_alignment;
            attributes.visibility = declarator_visibility;
            attributes.has_section = declarator_has_section;
            attributes.has_visibility = declarator_has_visibility;
            create_status = minic_declaration_create_external_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                minic_type_is_const(declarator_element_type),
                declarator_is_weak,
                false,
                &attributes,
                &object_id);
            if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot declare extern object");
                }
                return false;
            }
        }
        if (!minic_declaration_mark_file_scope_object(parser->program, object_id)) {
            minic_parser_error(parser, "cannot promote extern object to file scope");
            return false;
        }
        if (declarator_alias_target != MINIC_GLOBAL_OBJECT_INVALID &&
            !minic_c0_global_object_set_alias(
                parser->program, object_id, declarator_alias_target)) {
            minic_parser_error(parser, "invalid GNU object alias declaration");
            return false;
        }
        if (object_was_existing && declarator_is_weak &&
            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
            minic_parser_error(parser, "GNU weak requires external object linkage");
            return false;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after extern object declaration");
}

bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan first_name_span;
    MinicType base_type;
    MinicType first_object_type;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       sizeof(section_name),
                                                       &section_name_length,
                                                       &has_section,
                                                       &explicit_alignment) ||
        !parse_extern_object_declarator(parser, base_type, &first_name_span, &first_object_type)) {
        return false;
    }
    return minic_parser_parse_extern_global_after_head(parser,
                                                       base_type,
                                                       first_object_type,
                                                       first_name_span,
                                                       section_name,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment,
                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                       false);
}

static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span,
                                       char *section_name,
                                       size_t section_capacity,
                                       size_t *section_name_length,
                                       bool *has_section,
                                       size_t *explicit_alignment) {
    MinicType object_type;
    MinicType declared_object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t element_count;
    bool inferred_bound;

    element_count = 0U;
    inferred_bound = false;
    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_pointer(element_type)) {
        return false;
    }
    {
        const MinicArrayType *outer_array;
        bool is_array;

        is_array = false;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, true, &object_type, &is_array) ||
            !is_array || !minic_type_is_array(object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot build static pointer array type");
            }
            return false;
        }
        outer_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (outer_array == NULL) {
            minic_parser_error(parser, "invalid static pointer array type");
            return false;
        }
        inferred_bound = outer_array->element_count == 0U && !outer_array->is_zero_length;
        element_count = outer_array->element_count;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    declared_object_type = object_type;
    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        bool discard_declared;

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(parser->program,
                                                              parser->source +
                                                                  name_span.begin.offset,
                                                              minic_parser_span_length(name_span),
                                                              object_type,
                                                              true,
                                                              minic_type_is_const(element_type),
                                                              &object_id)) {
                minic_parser_error(parser,
                                   "cannot create static pointer array tentative definition");
                return false;
            }
        } else {
            existing = minic_c0_program_global_object(parser->program, existing_id);
            existing_array =
                existing != NULL && minic_type_is_array(existing->type)
                    ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                    : NULL;
            declared_array =
                minic_c0_program_array_type(parser->program, object_type.array_type_id);
            discard_declared =
                object_type.array_type_id + 1U == parser->program->array_type_count &&
                (existing == NULL || existing->type.array_type_id != object_type.array_type_id);
            if (existing == NULL || existing_array == NULL || declared_array == NULL ||
                !existing->is_internal ||
                !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
                existing_array->element_count != declared_array->element_count ||
                !minic_c0_global_object_merge_tentative(parser->program, existing_id)) {
                minic_parser_error(parser, "conflicting static pointer array tentative definition");
                return false;
            }
            object_id = existing_id;
            if (discard_declared &&
                !minic_c0_program_discard_last_array_type(parser->program, object_type)) {
                minic_parser_error(parser,
                                   "cannot retire transient static pointer array declaration");
                return false;
            }
        }
        if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static pointer array metadata");
            return false;
        }
        return minic_parser_advance(parser);
    }

    if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create static pointer array object");
            return false;
        }
    } else {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        size_t declared_count;
        bool discard_declared;

        existing = minic_c0_program_global_object(parser->program, existing_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        declared_count = declared_array == NULL ? 0U : declared_array->element_count;
        discard_declared =
            object_type.array_type_id + 1U == parser->program->array_type_count &&
            (existing == NULL || existing->type.array_type_id != object_type.array_type_id);
        if (existing == NULL || existing_array == NULL || declared_array == NULL ||
            !existing->is_internal || !existing->is_tentative ||
            !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
            (existing_array->element_count != 0U && declared_count != 0U &&
             existing_array->element_count != declared_count) ||
            (existing_array->element_count == 0U && declared_count != 0U &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static pointer array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
        existing_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (existing_array == NULL) {
            return false;
        }
        element_count = existing_array->element_count;
        inferred_bound = element_count == 0U && !existing_array->is_zero_length;
        if (discard_declared &&
            !minic_c0_program_discard_last_array_type(parser->program, declared_object_type)) {
            minic_parser_error(parser, "cannot retire transient static pointer array definition");
            return false;
        }
    }

    if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
        minic_parser_error(parser, "cannot persist static pointer array metadata");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, "expected '=' after static pointer array")) {
        return false;
    }
    {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (array_type == NULL) {
            minic_parser_error(parser, "invalid static pointer array type");
            return false;
        }
        if (array_type->is_zero_length) {
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_LBRACE, "expected '{' for zero-length array initializer") ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RBRACE, "zero-length array initializer must be empty")) {
                return false;
            }
        } else if (!minic_parser_static_scalar_array_transaction(
                       parser, object_id, element_type, element_count, inferred_bound)) {
            return false;
        }
    }
    {
        MinicType declaration_base;

        /* Pointer stars are per declarator in C: `char *a[2], *b[2]`
         * describes two pointer arrays, not a pointer and a double pointer. */
        if (!minic_type_pointee(element_type, &declaration_base)) {
            minic_parser_error(parser, "invalid static pointer-array declaration base");
            return false;
        }
        return parse_static_declarator_tail(parser, declaration_base);
    }
}

static bool static_object_type_is_read_only(const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;

    if (!minic_type_is_array(type)) {
        return minic_type_is_const(type);
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    return array_type != NULL && static_object_type_is_read_only(program, array_type->element_type);
}

bool minic_parser_global_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_float(object_type) && !minic_type_is_double(object_type) &&
         !minic_type_is_record(object_type) && !minic_type_is_array(object_type))) {
        return false;
    }
    if (!minic_c0_type_is_complete_object(parser->program, object_type)) {
        minic_parser_error(parser, "static object requires a complete object type");
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_tentative_global_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                true,
                static_object_type_is_read_only(parser->program, object_type),
                &object_id)) {
            minic_parser_error(parser, "cannot create static tentative definition");
            return false;
        }
    } else {
        existing = minic_c0_program_global_object(parser->program, object_id);
        if (existing == NULL || !existing->is_internal ||
            !minic_type_equal(existing->type, object_type) ||
            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
            minic_parser_error(parser, "conflicting static tentative definition");
            return false;
        }
    }
    return minic_parser_advance(parser);
}

static bool parse_static_inferred_integer_array(MinicParser *parser,
                                                MinicType element_type,
                                                MinicSourceSpan name_span,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_integer(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static integer array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse inferred static integer array");
        }
        return false;
    }
    if (minic_parser_static_string_element_compatible(
            parser, element_type, parser->current.kind)) {
        size_t string_count;

        if (!minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
            !minic_c0_program_complete_array_type(parser->program, object_type, string_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred static wide array");
            }
            return false;
        }
    } else if (!minic_parser_static_scalar_array_transaction(
                   parser, object_id, element_type, 0U, true)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse inferred static integer array");
        }
        return false;
    }
    return parse_static_declarator_tail(parser, element_type);
}

static bool parse_static_inferred_char_array(MinicParser *parser,
                                             MinicType element_type,
                                             MinicSourceSpan name_span,
                                             char *section_name,
                                             size_t section_capacity,
                                             size_t *section_name_length,
                                             bool *has_section,
                                             size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_char_integer(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static character array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin inferred static character array");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
            !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred static character array");
            }
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!minic_parser_static_scalar_array_transaction(parser, object_id, element_type, 0U, true)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(
                    parser, "cannot initialize inferred static character array from scalar list");
            }
            return false;
        }
    } else {
        minic_parser_error(
            parser,
            "inferred static character array requires a string or braced scalar initializer");
        return false;
    }
    return parse_static_declarator_tail(parser, element_type);
}

bool minic_parser_parse_static_zero_declaration_list_after_head(MinicParser *parser,
                                                                MinicType base_type,
                                                                MinicType first_object_type,
                                                                MinicSourceSpan first_name_span,
                                                                const char *shared_section_name,
                                                                size_t shared_section_name_length,
                                                                bool shared_has_section,
                                                                size_t shared_explicit_alignment) {
    bool first_declarator;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_COMMA ||
        (shared_has_section && (shared_section_name == NULL || shared_section_name_length == 0U ||
                                shared_section_name_length >= 256U))) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        char section_name[256];
        size_t section_name_length;
        size_t explicit_alignment;
        bool has_section;

        section_name_length = shared_section_name_length;
        explicit_alignment = shared_explicit_alignment;
        has_section = shared_has_section;
        (void)memset(section_name, 0, sizeof(section_name));
        if (shared_has_section) {
            (void)memcpy(section_name, shared_section_name, shared_section_name_length);
            section_name[shared_section_name_length] = '\0';
        }

        if (first_declarator) {
            object_type = first_object_type;
            name_span = first_name_span;
            first_declarator = false;
        } else {
            if (!minic_parser_parse_pointer_declarator(parser, base_type, &object_type) ||
                parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "expected static object declarator after ','");
                }
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }

        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           sizeof(section_name),
                                                           &section_name_length,
                                                           &has_section,
                                                           &explicit_alignment)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if ((!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
                 !minic_type_is_record(object_type)) ||
                (minic_type_is_record(object_type) &&
                 !minic_parser_require_complete_object_type(
                     parser, object_type, "static object requires a complete record type")) ||
                !minic_parser_global_begin_definition(parser, object_type, name_span, &object_id) ||
                !minic_parser_global_apply_metadata(parser,
                                              object_id,
                                              section_name,
                                              section_name_length,
                                              has_section,
                                              explicit_alignment) ||
                !minic_parser_advance(parser)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "cannot begin initialized static declaration-list declarator");
                }
                return false;
            }
            if (minic_type_is_pointer(object_type)) {
                if (!minic_parser_parse_static_pointer_object_initializer(
                        parser, object_id, object_type)) {
                    return false;
                }
            } else if (minic_type_is_integer(object_type)) {
                if (!minic_parser_parse_static_integer_object_initializer(
                        parser, object_id, object_type)) {
                    if (parser->diagnostic != NULL &&
                        parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot initialize static scalar declaration-list declarator");
                    }
                    return false;
                }
            } else if (!minic_parser_parse_static_storage_initializer_value(
                           parser, object_id, object_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "cannot initialize static declaration-list aggregate");
                }
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
                return minic_parser_advance(parser);
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA ||
                !minic_parser_advance(parser)) {
                minic_parser_error(
                    parser, "expected ',' or ';' after static declaration-list initializer");
                return false;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA &&
            parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(
                parser, "expected ',', '=' or ';' after static declaration-list declarator");
            return false;
        }
        if ((!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
             !minic_type_is_record(object_type)) ||
            (minic_type_is_record(object_type) &&
             !minic_parser_require_complete_object_type(
                 parser, object_type, "static object requires a complete record type"))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported static zero-definition declarator type");
            }
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    object_type,
                    true,
                    static_object_type_is_read_only(parser->program, object_type),
                    &object_id)) {
                minic_parser_error(parser, "cannot create static tentative declarator");
                return false;
            }
        } else {
            const MinicGlobalObject *existing;

            existing = minic_c0_program_global_object(parser->program, object_id);
            if (existing == NULL || !existing->is_internal ||
                !minic_type_equal(existing->type, object_type) ||
                !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
                minic_parser_error(parser, "conflicting static tentative declarator");
                return false;
            }
        }
        if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static tentative declarator metadata");
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return minic_parser_advance(parser);
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

static bool parse_preformed_static_array_definition(MinicParser *parser,
                                                    MinicType object_type,
                                                    MinicSourceSpan name_span,
                                                    const char *section_name,
                                                    size_t section_name_length,
                                                    bool has_section,
                                                    size_t explicit_alignment) {
    const MinicArrayType *array_type;
    MinicType instance_type;
    MinicGlobalObjectId object_id;

    if (parser == NULL || !minic_type_is_array(object_type) ||
        parser->current.kind != MINIC_TOKEN_EQUAL) {
        if (parser != NULL) {
            minic_parser_error(parser, "pre-formed static array definition requires '='");
        }
        return false;
    }
    array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
    if (array_type == NULL || (array_type->element_count == 0U && array_type->is_zero_length)) {
        minic_parser_error(parser, "invalid pre-formed static array definition type");
        return false;
    }
    if (!minic_declaration_instantiate_incomplete_array_object_type(
            parser->program, object_type, &instance_type) ||
        !minic_c0_program_add_global_object(
            parser->program,
            parser->source + name_span.begin.offset,
            minic_parser_span_length(name_span),
            instance_type,
            true,
            static_object_type_is_read_only(parser->program, object_type),
            &object_id) ||
        !minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment) ||
        !minic_parser_advance(parser)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin pre-formed static array definition");
        }
        return false;
    }

    object_type = instance_type;
    array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
    if (array_type == NULL) {
        minic_parser_error(parser, "invalid pre-formed static array descriptor");
        return false;
    }
    if (minic_type_is_char_integer(array_type->element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t string_count;
        bool infer_bound;

        infer_bound = array_type->element_count == 0U && !array_type->is_zero_length;
        if (infer_bound) {
            if (!minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
                !minic_c0_program_complete_array_type(parser->program, object_type, string_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot infer pre-formed character array extent");
                }
                return false;
            }
        } else if (!minic_parser_add_bounded_string_literal_initializer(
                       parser, object_id, array_type->element_count)) {
            return false;
        }
    } else if (!minic_parser_parse_static_storage_initializer_value(
                   parser, object_id, object_type)) {
        return false;
    }
    return parse_static_declarator_tail(parser, object_type);
}

bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_object_id;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_float(element_type) && !minic_type_is_double(element_type) &&
         !minic_type_is_record(element_type) && !minic_type_is_array(element_type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "unsupported static global object type");
        }
        return false;
    }
    existing_object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_global_zero_definition(parser, element_type, name_span);
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID &&
        (minic_type_is_array(element_type) || parser->current.kind == MINIC_TOKEN_LBRACKET)) {
        const MinicGlobalObject *existing;

        existing = minic_c0_program_global_object(parser->program, existing_object_id);
        if (existing == NULL || !existing->is_internal || !existing->is_tentative ||
            !minic_type_is_array(existing->type)) {
            minic_parser_error(parser, "duplicate global object");
            return false;
        }
    }
    if (minic_type_is_array(element_type)) {
        return parse_preformed_static_array_definition(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       *section_name_length,
                                                       *has_section,
                                                       *explicit_alignment);
    }
    if (minic_type_is_record(element_type)) {
        return minic_parser_static_record_initializer(parser,
                                   element_type,
                                   name_span,
                                   section_name,
                                   section_capacity,
                                   section_name_length,
                                   has_section,
                                   explicit_alignment);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_scalar(parser, element_type, name_span);
    }
    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
    if (!minic_type_is_integer(element_type) && !minic_type_is_record(element_type)) {
        minic_parser_error(parser,
                           "static array requires an integer, pointer, or record element type");
        return false;
    }
    {
        MinicParser probe;
        bool incomplete_multidimensional;

        incomplete_multidimensional = false;
        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            incomplete_multidimensional = probe.current.kind == MINIC_TOKEN_LBRACKET;
            if (!incomplete_multidimensional) {
                if (minic_type_is_char_integer(element_type)) {
                    return parse_static_inferred_char_array(parser,
                                                            element_type,
                                                            name_span,
                                                            section_name,
                                                            section_capacity,
                                                            section_name_length,
                                                            has_section,
                                                            explicit_alignment);
                }
                return parse_static_inferred_integer_array(parser,
                                                           element_type,
                                                           name_span,
                                                           section_name,
                                                           section_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment);
            }
        }
        {
            bool is_array;

            if (!minic_parser_parse_array_declarator_suffix(
                    parser, element_type, incomplete_multidimensional, &object_type, &is_array) ||
                !is_array ||
                !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                               section_name,
                                                               section_capacity,
                                                               section_name_length,
                                                               has_section,
                                                               explicit_alignment)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot build fixed static array type");
                }
                return false;
            }
        }
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        MinicType transient_type;
        bool discard_transient;

        existing = minic_c0_program_global_object(parser->program, existing_object_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        transient_type = object_type;
        discard_transient = existing != NULL &&
                            existing->type.array_type_id != object_type.array_type_id &&
                            object_type.array_type_id + 1U == parser->program->array_type_count;
        if (existing == NULL || existing_array == NULL || declared_array == NULL ||
            !existing->is_internal || !existing->is_tentative ||
            !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
            (existing_array->element_count != 0U && declared_array->element_count != 0U &&
             existing_array->element_count != declared_array->element_count) ||
            (existing_array->element_count == 0U && declared_array->element_count != 0U &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_array->element_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_object_id)) {
            minic_parser_error(parser, "conflicting static array definition");
            return false;
        }
        object_id = existing_object_id;
        object_type = parser->program->global_objects[object_id].type;
        if (discard_transient &&
            !minic_c0_program_discard_last_array_type(parser->program, transient_type)) {
            minic_parser_error(parser, "cannot retire transient static array definition type");
            return false;
        }
    } else if (!minic_c0_program_add_global_object(parser->program,
                                                   parser->source + name_span.begin.offset,
                                                   minic_parser_span_length(name_span),
                                                   object_type,
                                                   true,
                                                   minic_type_is_const(element_type),
                                                   &object_id)) {
        minic_parser_error(parser, "cannot add fixed static array object");
        return false;
    }
    if (!minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
        minic_parser_error(parser, "cannot persist fixed static array metadata");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            minic_parser_error(parser, "cannot zero-initialize fixed static array object");
            return false;
        }
        return minic_parser_advance(parser);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse fixed static array initializer");
        }
        return false;
    }
    return parse_static_declarator_tail(parser, element_type);
}

bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (parser == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }
    name_span = parser->current.span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (!minic_parser_parse_static_global_after_head(parser,
                                                     object_type,
                                                     name_span,
                                                     section_name,
                                                     sizeof(section_name),
                                                     &section_name_length,
                                                     &has_section,
                                                     &explicit_alignment)) {
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_parser_global_apply_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment)) {
        minic_parser_error(parser, "cannot persist static object metadata");
        return false;
    }
    return true;
}
