from pathlib import Path


def replace_once(path_name: str, old: str, new: str) -> None:
    path = Path(path_name)
    source = path.read_text()
    if old not in source:
        if new in source:
            return
        raise SystemExit(f"patch anchor not found: {path_name}: {old[:120]!r}")
    path.write_text(source.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    """static bool
pointer_relational_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           type_is_complete_object(program, left_pointee) &&
           type_is_complete_object(program, right_pointee);
}
""",
    """static bool
pointer_relational_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_pointee;
    MinicType right_pointee;
    MinicType left_unqualified;
    MinicType right_unqualified;

    return minic_type_pointee(left, &left_pointee) && minic_type_pointee(right, &right_pointee) &&
           minic_type_unqualified(left_pointee, &left_unqualified) &&
           minic_type_unqualified(right_pointee, &right_unqualified) &&
           minic_type_equal(left_unqualified, right_unqualified) &&
           pointer_arithmetic_pointee_allowed(program, left_unqualified) &&
           pointer_arithmetic_pointee_allowed(program, right_unqualified);
}
""",
)

replace_once(
    "tests/compiler/c0/run-runtime.sh",
    "run_case gnu_cleanup_runtime 0 gnu_cleanup_runtime\nrun_double_return_abi\n",
    "run_case gnu_cleanup_runtime 0 gnu_cleanup_runtime\n"
    "run_case gnu_void_pointer_relational 0 gnu_void_pointer_relational\n"
    "run_double_return_abi\n",
)
