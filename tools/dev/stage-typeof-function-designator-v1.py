#!/usr/bin/env python3
from pathlib import Path

parser_path = Path('src/frontend/parser_type.c')
text = parser_path.read_text()
old = '''            parsed_type = operand->type;
            if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&
                !minic_parser_materialize_array_object_type(parser, operand_id, &parsed_type)) {
'''
new = '''            parsed_type = operand->type;
            if (operand->kind == MINIC_EXPRESSION_FUNCTION &&
                (!minic_type_pointee(operand->type, &parsed_type) ||
                 !minic_type_is_function(parsed_type))) {
                minic_parser_error(parser, "cannot preserve GNU typeof function designator");
                return false;
            }
            if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&
                !minic_parser_materialize_array_object_type(parser, operand_id, &parsed_type)) {
'''
assert text.count(old) == 1
parser_path.write_text(text.replace(old, new, 1))

fixture_path = Path('tests/compiler/c0/typeof_generic.c')
fixture = fixture_path.read_text()
anchor = '''extern int generic_side_effect(void);

'''
addition = '''extern int generic_side_effect(void);

int typeof_function_redeclaration(int value) {
    return value;
}
extern typeof(typeof_function_redeclaration) typeof_function_redeclaration;

'''
assert fixture.count(anchor) == 1
fixture_path.write_text(fixture.replace(anchor, addition, 1))

runner_path = Path('tests/compiler/c0/run-typeof-generic.sh')
runner = runner_path.read_text()
old = '''              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size \\
              typeof_incomplete_object_address; do
'''
new = '''              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size \\
              typeof_incomplete_object_address typeof_function_redeclaration; do
'''
assert runner.count(old) == 1
runner = runner.replace(old, new, 1)
old = '''grep -F 'typeof_pending_object' "$assembly" >/dev/null

cat >"$work/incomplete-sizeof.c" <<'EOF'
'''
new = '''grep -F 'typeof_pending_object' "$assembly" >/dev/null
# GNU typeof of a function designator preserves the function type rather than
# the execution-time function-pointer representation used by ordinary expressions.
test "$(grep -c '^typeof_function_redeclaration:$' "$assembly")" -eq 1
grep -F '.globl typeof_function_redeclaration' "$assembly" >/dev/null

cat >"$work/incomplete-sizeof.c" <<'EOF'
'''
assert runner.count(old) == 1
runner = runner.replace(old, new, 1)
old = "printf '%s\\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name,incomplete-type-preserved generic=typed,default controlling=unevaluated linux-shape=1 completeness=consumer-owned'\n"
new = "printf '%s\\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name,incomplete-type-preserved,function-designator-preserved generic=typed,default controlling=unevaluated linux-shape=1 completeness=consumer-owned'\n"
assert runner.count(old) == 1
runner_path.write_text(runner.replace(old, new, 1))
