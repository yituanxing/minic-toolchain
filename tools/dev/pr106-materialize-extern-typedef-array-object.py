#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_global.c",
    """        if (minic_type_is_function(object_type) || minic_type_is_array(object_type)) {\n            minic_parser_error(parser, \"unsupported extern object type\");\n            return false;\n        }\n""",
    """        if (minic_type_is_function(object_type)) {\n            minic_parser_error(parser, \"unsupported extern object type\");\n            return false;\n        }\n""",
)

(ROOT / "tests/compiler/c0/extern_typedef_array_object.c").write_text(
    """struct cpumask {\n    unsigned long bits[2];\n};\n\ntypedef struct cpumask cpumask_var_t[1];\nextern cpumask_var_t irq_default_affinity;\n\ntypedef unsigned long row_t[3];\nextern row_t matrix[2];\n\ntypedef int triple_t[3];\nextern triple_t values;\nextern int values[3];\n\nstruct cpumask *default_affinity(void) {\n    return &irq_default_affinity[0];\n}\n\nunsigned long *select_row(unsigned int index) {\n    return matrix[index];\n}\n\nint *values_ptr(void) {\n    return values;\n}\n\nunsigned long matrix_size(void) {\n    return sizeof(matrix);\n}\n\nunsigned long values_size(void) {\n    return sizeof(values);\n}\n"""
)

(ROOT / "tests/compiler/c0/invalid_extern_typedef_array_redeclaration.c").write_text(
    """typedef int triple_t[3];\nextern triple_t values;\nextern int values[4];\n\nint main(void) {\n    return 0;\n}\n"""
)

(ROOT / "tests/compiler/c0/run-extern-typedef-array-object.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/extern-typedef-array-object"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

preprocess extern_typedef_array_object
"$minic" -S "$work/extern_typedef_array_object.i" -o "$work/extern_typedef_array_object.s"
grep -F "  la a0, irq_default_affinity" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  la a0, matrix" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  la a0, values" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  li a0, 48" "$work/extern_typedef_array_object.s" >/dev/null
grep -F "  li a0, 12" "$work/extern_typedef_array_object.s" >/dev/null
for symbol in irq_default_affinity matrix values; do
    if grep -F ".type $symbol, @object" "$work/extern_typedef_array_object.s" >/dev/null || \
       grep -F "$symbol:" "$work/extern_typedef_array_object.s" >/dev/null; then
        echo "FAIL compiler/c0/extern_typedef_array_object: extern symbol $symbol emitted storage" >&2
        exit 1
    fi
done

preprocess invalid_extern_typedef_array_redeclaration
if "$minic" -S "$work/invalid_extern_typedef_array_redeclaration.i" \
    -o "$work/invalid_extern_typedef_array_redeclaration.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo "FAIL compiler/c0/invalid_extern_typedef_array_redeclaration: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "conflicting extern object redeclaration" "$work/invalid.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/extern_typedef_array_object typedef-array=direct linux-cpumask-shape=1 nested-suffix=array-of-array sizeof=48 redeclaration=compatible incompatible=reject storage=none"
'''
)

run = ROOT / "tests/compiler/c0/run.sh"
text = run.read_text()
line = 'MINIC="$minic" BUILD_DIR="$work/extern-typedef-array-object" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-extern-typedef-array-object.sh"\n'
if line not in text:
    if not text.endswith("\n"):
        text += "\n"
    text += "\n" + line
    run.write_text(text)
