from pathlib import Path

path = Path('src/frontend/parser_function.c')
text = path.read_text()

def replace_once(haystack, old, new, label):
    count = haystack.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, got {count}')
    return haystack.replace(old, new, 1)

text = replace_once(
    text,
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
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility,
                                         bool *is_weak) {''',
    'visible external array signature')

start = text.find('static bool parse_visible_external_array(')
end = text.find('typedef struct MinicParsedDeclarationPrefix', start)
if start < 0 or end < 0:
    raise SystemExit('visible external array function boundary changed')
region = text[start:end]
region = replace_once(
    region,
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {''',
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        is_weak == NULL) {''',
    'visible external array validation')

normal_call = 'minic_parser_parse_gnu_object_attribute_lists(parser,'
call_count = region.count(normal_call)
if call_count != 2:
    raise SystemExit(f'visible external array normal suffix parser count changed: {call_count}')
region = region.replace(normal_call,
                        'minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(parser,')

tail = '''                                                           explicit_alignment))'''
replacement = '''                                                           explicit_alignment,
                                                           &visibility,
                                                           &has_visibility,
                                                           is_weak))'''
# clang-format normalizes both calls to the same argument indentation after materialization.
tail_count = region.count(tail)
if tail_count != 2:
    # Accept the shorter indentation used by the complete-array branch before clang-format.
    tail = '''                                                       explicit_alignment))'''
    replacement = '''                                                       explicit_alignment,
                                                       &visibility,
                                                       &has_visibility,
                                                       is_weak))'''
    tail_count = region.count(tail)
if tail_count != 2:
    # Rewrite each parser call structurally instead of touching code outside this function.
    marker = 'explicit_alignment))'
    if region.count(marker) < 2:
        raise SystemExit('visible external array attribute-call tail changed')
    pieces = region.split(marker)
    rebuilt = pieces[0]
    replacements_left = 2
    for piece in pieces[1:]:
        if replacements_left > 0 and rebuilt.rstrip().endswith('explicit_alignment') is False:
            pass
        if replacements_left > 0:
            rebuilt += 'explicit_alignment,\n                                                           &visibility,\n                                                           &has_visibility,\n                                                           is_weak))'
            replacements_left -= 1
        else:
            rebuilt += marker
        rebuilt += piece
    region = rebuilt
else:
    region = region.replace(tail, replacement)

text = text[:start] + region + text[end:]

text = replace_once(
    text,
'''                                              visibility,
                                              has_visibility)) {''',
'''                                              visibility,
                                              has_visibility,
                                              &object_is_weak)) {''',
    'top-level external array call')

path.write_text(text)
