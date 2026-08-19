#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    p.write_text(text.replace(old, new, 1))


# Record arrays already carry an element type + element count through the generic
# layout/member machinery. Function pointers are ordinary complete pointer element
# types here; the old rejection predates that generic representation.
replace_once(
    "src/frontend/parser_record.c",
    """        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {\n            minic_parser_error(parser, \"function pointer field arrays are unsupported\");\n            return false;\n        }\n""",
    "",
    "function pointer record-field arrays",
)

# Alias target final-definition ownership already belongs to the post-parse AST
# verifier. Keep declaration-time signature/section checks, but do not reject a
# previously declared target merely because its definition appears later in the TU.
replace_once(
    "src/frontend/parser_function.c",
    """        if (alias_function == NULL || !alias_function->is_defined || has_section ||\n            !minic_parser_function_signature_matches(\n""",
    """        if (alias_function == NULL || has_section ||\n            !minic_parser_function_signature_matches(\n""",
    "forward GNU function alias",
)

# Extend the existing alias gate with the Linux ordering: declaration, alias,
# definition. A target that never becomes defined remains fail-closed at the
# translation-unit AST contract owner rather than at declaration time.
alias_gate = Path("tests/compiler/c0/run-gnu-function-copy-alias.sh")
text = alias_gate.read_text()
needle = """grep -Fq '.set alias_fn, target' \"$work/positive.s\"\n"""
insert = """grep -Fq '.set alias_fn, target' \"$work/positive.s\"\ncat > \"$work/forward.c\" <<'SRC'\nint target(int value);\nint __attribute__((weak, alias(\"target\"))) alias_fn(int value);\nint target(int value) { return value + 1; }\nSRC\n\"$minic\" -S \"$work/forward.c\" -o \"$work/forward.s\"\ngrep -Fq '.weak alias_fn' \"$work/forward.s\"\ngrep -Fq '.set alias_fn, target' \"$work/forward.s\"\n"""
if text.count(needle) != 1:
    raise SystemExit("forward alias gate insertion point mismatch")
text = text.replace(needle, insert, 1)
old_undefined = """grep -Fq 'GNU function alias requires a defined same-TU target with matching signature' \"$work/undefined.err\"\n"""
new_undefined = """grep -Fq 'parsed AST violates compiler contracts' \"$work/undefined.err\"\n"""
if text.count(old_undefined) != 1:
    raise SystemExit("undefined alias diagnostic owner insertion point mismatch")
alias_gate.write_text(text.replace(old_undefined, new_undefined, 1))

# Focused record/layout gate: typedef'd function pointer arrays are record fields,
# with ordinary pointer-size layout and a following scalar field.
Path("tests/compiler/c0/run-function-pointer-field-array.sh").write_text(
    """#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- \"$(dirname -- \"$0\")/../../..\" && pwd)\nminic=${MINIC:-\"$root/build/debug/bin/minic\"}\nhost_cc=${HOST_CC:-${CC:-cc}}\nwork=${BUILD_DIR:-\"$root/build/debug\"}/tests/function-pointer-field-array\nmkdir -p \"$work\"\ncat > \"$work/input.c\" <<'SRC'\ntypedef int (*filter_t)(int);\nstruct hook_filter {\n    filter_t filters[4];\n    unsigned int count;\n};\nstatic struct hook_filter state;\nint main(void)\n{\n    return sizeof(struct hook_filter) == 40 && sizeof(state.filters) == 32 ? 0 : 1;\n}\nSRC\n\"$host_cc\" -E -P -std=gnu11 -x c \"$work/input.c\" -o \"$work/input.i\"\n\"$minic\" -S \"$work/input.i\" -o \"$work/output.s\"\ngrep -F '.size state, 40' \"$work/output.s\" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/function-pointer-field-array element=function-pointer count=4 layout=generic'\n"""
)

run = Path("tests/compiler/c0/run.sh")
text = run.read_text()
needle = """MINIC=\"$minic\" \\\nHOST_CC=\"$host_cc\" \\\nBUILD_DIR=\"${BUILD_DIR:-\"$root/build/debug\"}\" \\\nsh \"$root/tests/compiler/c0/run-function-pointer-qualifiers.sh\"\n\n"""
insert = needle + """MINIC=\"$minic\" \\\nHOST_CC=\"$host_cc\" \\\nBUILD_DIR=\"${BUILD_DIR:-\"$root/build/debug\"}\" \\\nsh \"$root/tests/compiler/c0/run-function-pointer-field-array.sh\"\n\n"""
if text.count(needle) != 1:
    raise SystemExit("C0 function pointer gate insertion point mismatch")
run.write_text(text.replace(needle, insert, 1))

print("staged function-pointer record arrays + deferred GNU alias target definition")
