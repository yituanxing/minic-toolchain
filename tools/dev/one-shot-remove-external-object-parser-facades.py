from pathlib import Path

function_path = Path("src/frontend/parser_function.c")
text = function_path.read_text()
include_anchor = '#include "frontend/attribute.h"\n'
include_line = '#include "frontend/declaration_sema.h"\n'
if include_line not in text:
    if text.count(include_anchor) != 1:
        raise SystemExit("parser_function declaration sema include anchor is not unique")
    text = text.replace(include_anchor, include_anchor + include_line, 1)
if text.count("minic_parser_external_object_types_compatible(") != 3:
    raise SystemExit("unexpected parser external compatibility call count")
if text.count("minic_parser_merge_external_array_composite_type(") != 2:
    raise SystemExit("unexpected parser external composite call count")
text = text.replace(
    "minic_parser_external_object_types_compatible(",
    "minic_declaration_external_object_types_compatible(",
)
text = text.replace(
    "minic_parser_merge_external_array_composite_type(",
    "minic_declaration_merge_external_array_composite_type(",
)
function_path.write_text(text)

global_path = Path("src/frontend/parser_global.c")
text = global_path.read_text()
facades = '''bool minic_parser_external_object_types_compatible(const MinicC0Program *program,
                                                   MinicType existing_type,
                                                   MinicType declared_type) {
    return minic_declaration_external_object_types_compatible(
        program, existing_type, declared_type);
}

bool minic_parser_merge_external_array_composite_type(MinicC0Program *program,
                                                      MinicType existing_type,
                                                      MinicType declared_type) {
    return minic_declaration_merge_external_array_composite_type(
        program, existing_type, declared_type);
}

'''
if text.count(facades) != 1:
    raise SystemExit("parser_global facade block is not unique")
global_path.write_text(text.replace(facades, "", 1))

internal_path = Path("src/frontend/parser_internal.h")
text = internal_path.read_text()
prototypes = '''bool minic_parser_external_object_types_compatible(const MinicC0Program *program,
                                                   MinicType existing_type,
                                                   MinicType declared_type);
bool minic_parser_merge_external_array_composite_type(MinicC0Program *program,
                                                      MinicType existing_type,
                                                      MinicType declared_type);
'''
if text.count(prototypes) != 1:
    raise SystemExit("parser_internal facade prototypes are not unique")
internal_path.write_text(text.replace(prototypes, "", 1))
