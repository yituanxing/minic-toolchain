from pathlib import Path

path = Path('src/frontend/parser_function.c')
text = path.read_text()

def replace_once(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, got {count}')
    text = text.replace(old, new, 1)

replace_once(
'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         char *section_name,
                                         size_t section_name_capacity,
                                         size_t *section_name_length,
                                         bool *has_section,
                                         size_t *explicit_alignment,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {''',
'''static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         char *section_name,
                                         size_t section_name_capacity,
                                         size_t *section_name_length,
                                         bool *has_section,
                                         size_t *explicit_alignment,
                                         MinicSymbolVisibility *visibility,
                                         bool *has_visibility,
                                         bool *is_weak) {''',
'visible external array signature')

replace_once(
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {''',
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        visibility == NULL || has_visibility == NULL || is_weak == NULL) {''',
'visible external array validation')

replace_once(
'''            !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           section_name_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment)) {''',
'''            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                section_name_capacity,
                section_name_length,
                has_section,
                explicit_alignment,
                visibility,
                has_visibility,
                is_weak)) {''',
'tentative incomplete array symbol attrs')

# In the complete-array branch, external linkage also owns symbol suffix metadata.
replace_once(
'''        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_name_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {''',
'''        !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
            parser,
            section_name,
            section_name_capacity,
            section_name_length,
            has_section,
            explicit_alignment,
            visibility,
            has_visibility,
            is_weak)) {''',
'complete external array symbol attrs')

# Dereference shared metadata state when handing canonical metadata to object owners.
text = text.replace('''                                               visibility,
                                               has_visibility);''',
                    '''                                               *visibility,
                                               *has_visibility);''')
text = text.replace('''                                            visibility,
                                            has_visibility);''',
                    '''                                            *visibility,
                                            *has_visibility);''')

replace_once(
'''                                              visibility,
                                              has_visibility)) {''',
'''                                              &visibility,
                                              &has_visibility,
                                              &object_is_weak)) {''',
'top-level external array call')

path.write_text(text)
