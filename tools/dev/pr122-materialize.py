from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''    return minic_type_same_unqualified(left_pointee, right_pointee) &&
           minic_c0_type_is_complete_object(parser->program, left_pointee);
''',
    '''    return minic_type_same_unqualified(left_pointee, right_pointee) &&
           minic_c0_pointer_arithmetic_pointee_allowed(parser->program, left_pointee);
''',
)

fixture = Path("tests/compiler/c0/gnu_void_pointer_arithmetic.c")
text = fixture.read_text()
addition = r'''

long gnu_void_pointer_difference(void *left, void *right) {
    return left - right;
}

long linux_void_pointer_difference(void *ptr, unsigned char *data) {
    return ptr - (void *)data;
}

typedef int gnu_callback_type(int value);

long gnu_function_pointer_difference(gnu_callback_type *left,
                                     gnu_callback_type *right) {
    return left - right;
}
'''
if "gnu_void_pointer_difference" in text:
    raise SystemExit("GNU pointer difference fixture already materialized")
fixture.write_text(text + addition)

runner = Path("tests/compiler/c0/run-gnu-void-pointer-arithmetic.sh")
text = runner.read_text()
needle = "grep -F 'gnu_void_pointer_subtract:' \"$assembly\" >/dev/null\n"
addition = needle + (
    "grep -F 'gnu_void_pointer_difference:' \"$assembly\" >/dev/null\n"
    "grep -F 'linux_void_pointer_difference:' \"$assembly\" >/dev/null\n"
    "grep -F 'gnu_function_pointer_difference:' \"$assembly\" >/dev/null\n"
)
if text.count(needle) != 1:
    raise SystemExit(f"GNU pointer arithmetic label anchor mismatch: {text.count(needle)}")
text = text.replace(needle, addition, 1)

pass_anchor = "printf '%s\\n' 'PASS compiler/c0/gnu_void_pointer_arithmetic"
negative = r'''cat >"$work/mismatched-pointer-difference.c" <<'EOF'
long bad_difference(int *left, long *right)
{
    return left - right;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/mismatched-pointer-difference.c" \
    -o "$work/mismatched-pointer-difference.i"
if "$minic" -S "$work/mismatched-pointer-difference.i" \
    -o "$work/mismatched-pointer-difference.s" \
    2>"$work/mismatched-pointer-difference.stderr"; then
    printf '%s\n' 'mismatched pointer difference unexpectedly accepted' >&2
    exit 1
fi
grep -F 'unsupported pointer arithmetic operands' "$work/mismatched-pointer-difference.stderr" >/dev/null

cat >"$work/incomplete-pointer-difference.c" <<'EOF'
struct Incomplete;
long bad_incomplete_difference(struct Incomplete *left, struct Incomplete *right)
{
    return left - right;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-pointer-difference.c" \
    -o "$work/incomplete-pointer-difference.i"
if "$minic" -S "$work/incomplete-pointer-difference.i" \
    -o "$work/incomplete-pointer-difference.s" \
    2>"$work/incomplete-pointer-difference.stderr"; then
    printf '%s\n' 'incomplete object pointer difference unexpectedly accepted' >&2
    exit 1
fi
grep -F 'unsupported pointer arithmetic operands' "$work/incomplete-pointer-difference.stderr" >/dev/null

'''
pos = text.find(pass_anchor)
if pos < 0:
    raise SystemExit("GNU pointer arithmetic PASS anchor missing")
text = text[:pos] + negative + text[pos:]
text = text.replace(
    "pointee=void stride=1 binary=+,- incomplete-record=unchanged",
    "pointee=void/function stride=1 binary=+,- difference=void+function mismatched=reject incomplete-record=unchanged",
    1,
)
runner.write_text(text)
