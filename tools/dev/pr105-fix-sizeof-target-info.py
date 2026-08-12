#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = root / path
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected exactly one anchor, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/codegen_expression.c",
    '#include "target/riscv64/layout.h"\n',
    '#include "target/riscv64/layout.h"\n#include "target/target_info.h"\n',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_SIZEOF: {\n        MinicType measured_type;\n        size_t alignment;\n        size_t size;\n\n        measured_type = expression->value.sizeof_type;\n        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||\n            !minic_riscv64_type_layout(program, measured_type, &size, &alignment)) {\n            return false;\n        }\n        return fprintf(file, \"  li a0, %zu\\n\", size) >= 0;\n    }\n""",
    """    case MINIC_EXPRESSION_SIZEOF: {\n        MinicType measured_type;\n        size_t size;\n\n        measured_type = expression->value.sizeof_type;\n        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||\n            !minic_target_info_sizeof_type(\n                minic_default_target_info(), program, measured_type, &size)) {\n            return false;\n        }\n        return fprintf(file, \"  li a0, %zu\\n\", size) >= 0;\n    }\n""",
)

invalid = root / "tests/compiler/c0/invalid_extern_void_sizeof.c"
if invalid.exists():
    invalid.unlink()
(root / "tests/compiler/c0/gnu_extern_void_sizeof.c").write_text(
    """extern const void opaque_symbol;\n\nunsigned long opaque_size(void) {\n    return sizeof(opaque_symbol);\n}\n\nint main(void) {\n    return opaque_size() == 1UL ? 0 : 1;\n}\n"""
)

runner = root / "tests/compiler/c0/run-gnu-extern-void-symbol.sh"
text = runner.read_text()
old = 'expect_failure invalid_extern_void_sizeof "sizeof requires a supported complete type"\n'
new = '''preprocess gnu_extern_void_sizeof\n"$minic" -S "$work/gnu_extern_void_sizeof.i" -o "$work/gnu_extern_void_sizeof.s"\ngrep -F "  li a0, 1" "$work/gnu_extern_void_sizeof.s" >/dev/null\n'''
if text.count(old) != 1:
    raise SystemExit(f"runner: expected one sizeof negative anchor, found {text.count(old)}")
text = text.replace(old, new, 1)
text = text.replace("storage=none sizeof=reject definition=reject", "storage=none sizeof=gnu-byte definition=reject", 1)
runner.write_text(text)
