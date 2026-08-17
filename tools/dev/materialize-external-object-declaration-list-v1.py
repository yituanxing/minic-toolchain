from pathlib import Path

path = Path('src/frontend/parser_function.c')
text = path.read_text()

def replace_once(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, got {count}')
    text = text.replace(old, new, 1)

old = '''static bool parse_external_tentative_object(MinicParser *parser,
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

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_c0_type_is_complete_object(parser->program, object_type) &&
         !minic_type_is_array(object_type))) {
        if (parser != NULL) {
            minic_parser_error(
                parser,
                "external tentative definition requires an object or incomplete array type");
        }
        return false;
    }
'''
new = '''static bool record_external_tentative_object(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span,
                                             const char *section_name,
                                             size_t section_name_length,
                                             bool has_section,
                                             size_t explicit_alignment,
                                             MinicSymbolVisibility visibility,
                                             bool has_visibility,
                                             MinicGlobalObjectId *recorded_id) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;

    if (parser == NULL ||
        (!minic_c0_type_is_complete_object(parser->program, object_type) &&
         !minic_type_is_array(object_type))) {
        if (parser != NULL) {
            minic_parser_error(
                parser,
                "external tentative definition requires an object or incomplete array type");
        }
        return false;
    }
'''
replace_once(old, new, 'split tentative owner')
old = '''    if (!apply_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility)) {
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_external_object_definition'''
new = '''    if (!apply_external_object_metadata(parser,
                                        object_id,
                                        section_name,
                                        section_name_length,
                                        has_section,
                                        explicit_alignment,
                                        visibility,
                                        has_visibility)) {
        return false;
    }
    if (recorded_id != NULL) {
        *recorded_id = object_id;
    }
    return true;
}

static bool parse_external_tentative_object(MinicParser *parser,
                                            MinicType object_type,
                                            MinicSourceSpan name_span,
                                            const char *section_name,
                                            size_t section_name_length,
                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
                                            bool has_visibility) {
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
                                          NULL)) {
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_external_tentative_declaration_list_after_head(
    MinicParser *parser,
    MinicType base_type,
    MinicType first_type,
    MinicSourceSpan first_name,
    const char *first_section_name,
    size_t first_section_name_length,
    bool first_has_section,
    size_t first_explicit_alignment,
    MinicSymbolVisibility first_visibility,
    bool first_has_visibility,
    bool first_is_weak) {
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
    if (first_is_weak && !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
        minic_parser_error(parser, "GNU weak requires external object linkage");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicSourceSpan name_span;
        MinicType object_type;
        char section_name[128];
        size_t section_name_length;
        size_t explicit_alignment;
        MinicSymbolVisibility visibility;
        unsigned int const_qualifiers;
        unsigned int volatile_qualifiers;
        size_t pointer_depth;
        bool has_section;
        bool has_visibility;
        bool is_array;
        bool is_weak;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        object_type = base_type;
        pointer_depth = 0U;
        const_qualifiers = 0U;
        volatile_qualifiers = 0U;
        while (parser->current.kind == MINIC_TOKEN_STAR) {
            unsigned int bit;

            pointer_depth += 1U;
            if (pointer_depth > sizeof(unsigned int) * CHAR_BIT || !minic_parser_advance(parser) ||
                !minic_parser_parse_pointer_qualifier_sequence(parser,
                                                               pointer_depth,
                                                               &const_qualifiers,
                                                               &volatile_qualifiers) ||
                !minic_type_pointer_to(object_type, &object_type)) {
                minic_parser_error(parser, "cannot form external declaration-list pointer type");
                return false;
            }
            bit = 1U << (pointer_depth - 1U);
            if (((const_qualifiers & bit) != 0U &&
                 !minic_type_add_const(object_type, &object_type)) ||
                ((volatile_qualifiers & bit) != 0U &&
                 !minic_type_add_volatile(object_type, &object_type))) {
                return false;
            }
        }
        if (!minic_parser_parse_direct_declarator_name(parser, &name_span) ||
            !minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array)) {
            return false;
        }
        (void)is_array;
        (void)memset(section_name, 0, sizeof(section_name));
        section_name_length = 0U;
        explicit_alignment = 0U;
        visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
        has_section = false;
        has_visibility = false;
        is_weak = false;
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &explicit_alignment,
                &visibility,
                &has_visibility,
                &is_weak) ||
            parser->current.kind == MINIC_TOKEN_EQUAL ||
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
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(
                    parser,
                    "external declaration-list v1 supports declaration-only object declarators");
            }
            return false;
        }
        if (is_weak && !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
            minic_parser_error(parser, "GNU weak requires external object linkage");
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object declaration list");
}

static bool parse_external_object_definition'''
replace_once(old, new, 'add external declaration list owner')

old = '''        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_visible_external_array(parser,'''
new = '''        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return parse_external_tentative_declaration_list_after_head(parser,
                                                                         base_type,
                                                                         return_type,
                                                                         name_span,
                                                                         section_name,
                                                                         section_name_length,
                                                                         has_section,
                                                                         object_explicit_alignment,
                                                                         visibility,
                                                                         has_visibility,
                                                                         object_is_weak);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_visible_external_array(parser,'''
replace_once(old, new, 'route external declaration list')

path.write_text(text)
