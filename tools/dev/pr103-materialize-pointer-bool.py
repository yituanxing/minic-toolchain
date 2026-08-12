#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
type_c = root / "src/frontend/type.c"
text = type_c.read_text()
old = '''bool minic_type_assignment_compatible(MinicType target, MinicType source) {\n    MinicType unqualified_target;\n    MinicType unqualified_source;\n\n    if ((minic_type_is_integer(target) && minic_type_is_integer(source)) ||\n        (minic_type_is_float(target) && minic_type_is_float(source)) ||\n        (minic_type_is_double(target) && minic_type_is_double(source))) {\n        return true;\n    }\n'''
new = '''bool minic_type_assignment_compatible(MinicType target, MinicType source) {\n    MinicType unqualified_target;\n    MinicType unqualified_source;\n\n    if ((minic_type_is_integer(target) && minic_type_is_integer(source)) ||\n        (minic_type_is_bool_integer(target) && minic_type_is_pointer(source)) ||\n        (minic_type_is_float(target) && minic_type_is_float(source)) ||\n        (minic_type_is_double(target) && minic_type_is_double(source))) {\n        return true;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"assignment compatibility anchor count={text.count(old)}")
type_c.write_text(text.replace(old, new, 1))

(root / "tests/compiler/c0/pointer_to_bool_conversion.c").write_text(r'''typedef unsigned int poll_mask_t;
typedef poll_mask_t (*poll_fn_t)(void *file, void *table);

_Bool return_function_pointer(poll_fn_t poll) {
    return poll;
}

_Bool return_object_pointer(void *pointer) {
    return pointer;
}

int assign_function_pointer(poll_fn_t poll) {
    _Bool available;
    available = poll;
    return available;
}

int assign_object_pointer(void *pointer) {
    _Bool available;
    available = pointer;
    return available;
}

static int accept_bool(_Bool value) {
    return value;
}

int pass_function_pointer(poll_fn_t poll) {
    return accept_bool(poll);
}
''')

(root / "tests/compiler/c0/invalid_pointer_to_int_return.c").write_text(r'''int invalid_pointer_return(void *pointer) {
    return pointer;
}
''')

(root / "tests/compiler/c0/run-pointer-to-bool-conversion.sh").write_text(r'''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-bool
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_to_bool_conversion.c" -o "$work/valid.i"
"$minic" -S "$work/valid.i" -o "$work/valid.s"

# Return and assignment boundaries already normalize integer targets through
# the target type. Pointer-to-bool therefore must produce real 0/1 values.
count=$(grep -c 'snez .*' "$work/valid.s" || true)
test "$count" -ge 4

# A fixed bool parameter is another assignment-conversion boundary. Require
# normalization in the caller before the direct call, not merely AST acceptance.
awk '
  /pass_function_pointer:/ { in_fn=1; saw=0 }
  in_fn && /snez a0, a0/ { saw=1 }
  in_fn && /call accept_bool/ { exit saw ? 0 : 1 }
  in_fn && /^\.size[[:space:]]+pass_function_pointer/ { exit 1 }
  END { if (!in_fn) exit 1 }
' "$work/valid.s"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_pointer_to_int_return.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo 'expected pointer-to-int return to remain rejected' >&2
    exit 1
fi
grep -F 'return expression does not match function return type' "$work/invalid.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/pointer_to_bool_conversion return=function+object assignment=function+object fixed-call=normalized pointer-to-int=reject'
''')

run = root / "tests/compiler/c0/run.sh"
run_text = run.read_text()
anchor = '''MINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh"\n'''
insert = anchor + '''\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-pointer-to-bool-conversion.sh"\n'''
if run_text.count(anchor) != 1:
    raise SystemExit(f"C0 gate insertion anchor count={run_text.count(anchor)}")
run.write_text(run_text.replace(anchor, insert, 1))
