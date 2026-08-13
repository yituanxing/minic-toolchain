#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/frontend/parser_function.c'
text = path.read_text()
old = '''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_parameter_suffix(parser, *parameter_type, parameter_type)) {
        minic_parser_error(parser, "cannot parse adjusted array parameter declarator");
        return false;
    }
    return true;
}
'''
new = '''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    if (parser == NULL || parameter_type == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_parse_array_parameter_suffix(parser, *parameter_type, parameter_type)) {
            minic_parser_error(parser, "cannot parse adjusted array parameter declarator");
            return false;
        }
        return true;
    }
    if (minic_type_is_array(*parameter_type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, parameter_type->array_type_id);
        if (array_type == NULL || !minic_type_pointer_to(array_type->element_type, parameter_type)) {
            minic_parser_error(parser, "cannot adjust typedef array parameter to pointer");
            return false;
        }
    }
    return true;
}
'''
if text.count(old) != 1:
    raise SystemExit('array parameter adjustment anchor missing')
path.write_text(text.replace(old, new, 1))

source_path = root / 'tests/compiler/c0/array_parameter_adjustment.c'
source = source_path.read_text()
marker = '\nint main(void) {'
insert = '''\ntypedef struct node node_vector[1];\ntypedef int matrix_alias[2][3];\n\nstatic void consume_typedef_vector(node_vector items) {\n    (void)items;\n}\n\nstatic int consume_typedef_matrix(matrix_alias matrix) {\n    return matrix[1][2];\n}\n'''
if source.count(marker) != 1:
    raise SystemExit('array parameter focused source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-array-parameter-adjustment.sh'
run = run_path.read_text()
old_msg = "'PASS compiler/c0/array_parameter_adjustment incomplete=pointer fixed-outer=pointer multidim-inner=retained verifier=no-orphan'"
new_msg = "'PASS compiler/c0/array_parameter_adjustment explicit=incomplete+fixed+multidim typedef=array+multidim adjusted=pointer verifier=no-orphan'"
if run.count(old_msg) != 1:
    raise SystemExit('array parameter focused message anchor missing')
run = run.replace(old_msg, new_msg, 1)
needle = '''grep -F '.globl main' "$work/output.s" >/dev/null\n'''
extra = needle + '''grep -F 'consume_typedef_vector:' "$work/output.s" >/dev/null\ngrep -F 'consume_typedef_matrix:' "$work/output.s" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('array parameter focused assertion anchor missing')
run_path.write_text(run.replace(needle, extra, 1))
