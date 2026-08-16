#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''                !minic_type_is_integer(value_expression->type) ||
                !minic_type_pointee(target_type, &pointee_type) ||
                !minic_c0_type_is_complete_object(parser->program, pointee_type)) {
''',
    '''                !minic_type_is_integer(value_expression->type) ||
                !minic_type_pointee(target_type, &pointee_type) ||
                !minic_c0_pointer_arithmetic_pointee_allowed(parser->program, pointee_type)) {
''',
    "compound assignment GNU pointer arithmetic semantic owner",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   minic_c0_type_is_complete_object(program, pointee_type);
''',
    '''                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   minic_c0_pointer_arithmetic_pointee_allowed(program, pointee_type);
''',
    "compound assignment verifier GNU pointer arithmetic owner",
)

source = Path("tests/compiler/c0/compound_assignment_expression.c")
text = source.read_text()
old = '''static int *advance_pointer(int *pointer) {
    pointer += 2;
    return pointer;
}

'''
new = old + '''static void *advance_void_pointer(void *pointer) {
    pointer += 3;
    return pointer;
}

'''
if text.count(old) != 1:
    raise SystemExit(f"compound source pointer anchor: expected 1 match, found {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    return update_once() == 14 && advance_pointer(values) == values + 2 &&
                   divide_unsigned(100ULL) == 10ULL && divide_signed(-100LL) == -10LL
'''
new = '''    return update_once() == 14 && advance_pointer(values) == values + 2 &&
                   advance_void_pointer(values) == (void *)((char *)values + 3) &&
                   divide_unsigned(100ULL) == 10ULL && divide_signed(-100LL) == -10LL
'''
if text.count(old) != 1:
    raise SystemExit(f"compound source main anchor: expected 1 match, found {text.count(old)}")
source.write_text(text.replace(old, new, 1))

runner = Path("tests/compiler/c0/run-compound-assignment-expressions.sh")
text = runner.read_text()
old = "printf '%s\\n' 'PASS compiler/c0/compound_assignment_expression operators=+=,/= result=value lvalue-evaluation=once pointer-scale=4 divide=signed,unsigned'\n"
new = "printf '%s\\n' 'PASS compiler/c0/compound_assignment_expression operators=+=,/= result=value lvalue-evaluation=once pointer-scale=4 GNU-void-stride=1 divide=signed,unsigned'\n"
if text.count(old) != 1:
    raise SystemExit(f"compound runner message anchor: expected 1 match, found {text.count(old)}")
runner.write_text(text.replace(old, new, 1))

print("staged GNU void/function pointer compound assignment through canonical pointer arithmetic owner")
