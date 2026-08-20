from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    file.write_text(text.replace(old, new, 1))


# Pointer subtraction and relational comparison are distinct C/GNU semantics.
# Keep one Program-owned compatibility helper for each and make parser/verifier share it.
header_old = '''bool minic_c0_pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                                 MinicType pointee_type);
bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right);
'''
header_new = '''bool minic_c0_pointer_arithmetic_pointee_allowed(const MinicC0Program *program,
                                                 MinicType pointee_type);
bool minic_c0_pointer_difference_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right);
bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right);
'''
replace_once("src/frontend/ast.h", header_old, header_new)

ast_old = '''bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right) {
'''
ast_new = '''bool minic_c0_pointer_difference_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return program != NULL && minic_type_pointee(left, &left_pointee) &&
           minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_c0_types_compatible(program, left_unqualified, right_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, left_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, right_unqualified);
}

bool minic_c0_pointer_relational_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right) {
'''
replace_once("src/frontend/ast.c", ast_old, ast_new)

parser_helper = '''static bool
pointer_difference_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, left_unqualified);
}

'''
replace_once("src/frontend/parser_expression.c", parser_helper, "")
replace_once(
    "src/frontend/parser_expression.c",
    "        pointer_difference_compatible(program, left, right)) {\n",
    "        minic_c0_pointer_difference_compatible(program, left, right)) {\n",
)

verifier_old = '''        return minic_type_equal(expression->type, minic_type_long()) &&
               minic_c0_pointer_relational_compatible(program, left->type, right->type);
'''
verifier_new = '''        return minic_type_equal(expression->type, minic_type_long()) &&
               minic_c0_pointer_difference_compatible(program, left->type, right->type);
'''
replace_once("src/frontend/ast_verifier.c", verifier_old, verifier_new)
