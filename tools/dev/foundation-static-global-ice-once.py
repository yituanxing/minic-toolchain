#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_all(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count == 0:
        raise SystemExit(f"{label}: expected at least one anchor")
    return text.replace(old, new)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/parser_global.c"
text = path.read_text()
old = '''    if (minic_type_is_integer(type)) {\n        int value;\n\n        if (!minic_parser_parse_integer_value(parser, &value) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static integer initializer");\n            }\n            return false;\n        }\n'''
new = '''    if (minic_type_is_integer(type)) {\n        int64_t constant_value;\n        int value;\n\n        if (!minic_parser_parse_integer_constant_expression(parser, &constant_value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser,\n                                   "static integer initializer requires an integer constant expression");\n            }\n            return false;\n        }\n        if (constant_value < INT_MIN || constant_value > INT_MAX) {\n            minic_parser_error(parser, "static integer initializer is out of supported range");\n            return false;\n        }\n        value = (int)constant_value;\n        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n            minic_parser_error(parser, "cannot record static integer initializer");\n            return false;\n        }\n'''
text = replace_once(text, old, new, "static-global-shared-ice")
path.write_text(text)

path = root / "src/frontend/parser_core.c"
text = path.read_text()
text = replace_all(
    text,
    "expected integer constant expression in array bound",
    "expected integer constant expression",
    "shared-ice-primary-diagnostic",
)
text = replace_all(
    text,
    "array bound constant expression overflow",
    "integer constant expression overflow",
    "shared-ice-overflow-diagnostic",
)
text = replace_all(
    text,
    "division by zero in array bound constant expression",
    "division by zero in integer constant expression",
    "shared-ice-divzero-diagnostic",
)
path.write_text(text)

path = root / "src/target/riscv64/codegen_function.c"
text = path.read_text()
text = replace_once(
    text,
    '''static bool minic_riscv64_global_scalar_type(const MinicC0Program *program,\n''',
    '''static bool minic_riscv64_integer_storage_width(const MinicC0Program *program,\n                                                 MinicType type,\n                                                 size_t *width) {\n    size_t alignment;\n    size_t size;\n\n    if (program == NULL || width == NULL || !minic_type_is_integer(type) ||\n        !minic_riscv64_type_layout(program, type, &size, &alignment) ||\n        (size != 1U && size != 2U && size != 4U && size != 8U)) {\n        return false;\n    }\n    (void)alignment;\n    *width = size;\n    return true;\n}\n\nstatic const char *minic_riscv64_integer_data_directive(size_t width) {\n    return width == 1U ? ".byte"\n           : width == 2U ? ".half"\n           : width == 4U ? ".word"\n           : width == 8U ? ".dword"\n                         : NULL;\n}\n\nstatic bool minic_riscv64_global_scalar_type(const MinicC0Program *program,\n''',
    "rv64-integer-data-width-helper",
)
text = replace_once(
    text,
    '''    if (!minic_type_is_integer(type)) {\n        return false;\n    }\n    *scalar_type = type;\n    *scalar_width = minic_type_is_char_integer(type)    ? 1U\n                    : minic_type_is_short_integer(type) ? 2U\n                    : minic_type_is_long_integer(type)  ? 8U\n                                                        : 4U;\n    return true;\n''',
    '''    if (!minic_riscv64_integer_storage_width(program, type, scalar_width)) {\n        return false;\n    }\n    *scalar_type = type;\n    return true;\n''',
    "rv64-global-scalar-width",
)
text = replace_once(
    text,
    '''            directive = minic_type_is_char_integer(field->type)    ? ".byte"\n                        : minic_type_is_short_integer(field->type) ? ".half"\n                        : minic_type_is_long_integer(field->type)  ? ".dword"\n                                                                   : ".word";\n            if (minic_type_is_char_integer(field->type)) {\n''',
    '''            directive = minic_riscv64_integer_data_directive(field_size);\n            if (directive == NULL) {\n                return false;\n            }\n            if (field_size == 1U) {\n''',
    "rv64-direct-record-width",
)
text = replace_once(
    text,
    '''        directive = minic_type_is_char_integer(type)    ? ".byte"\n                    : minic_type_is_short_integer(type) ? ".half"\n                    : minic_type_is_long_integer(type)  ? ".dword"\n                                                        : ".word";\n        if (minic_type_is_char_integer(type)) {\n''',
    '''        directive = minic_riscv64_integer_data_directive(type_size);\n        if (directive == NULL) {\n            return false;\n        }\n        if (type_size == 1U) {\n''',
    "rv64-recursive-constant-width",
)
text = replace_once(
    text,
    '''            directive = minic_type_is_char_integer(field->type)    ? ".byte"\n                        : minic_type_is_short_integer(field->type) ? ".half"\n                        : minic_type_is_long_integer(field->type)  ? ".dword"\n                                                                   : ".word";\n            if (minic_type_is_char_integer(field->type)) {\n''',
    '''            directive = minic_riscv64_integer_data_directive(field_size);\n            if (directive == NULL) {\n                return false;\n            }\n            if (field_size == 1U) {\n''',
    "rv64-record-array-width",
)
text = replace_once(
    text,
    '''        directive = minic_type_is_char_integer(scalar_type)    ? ".byte"\n                    : minic_type_is_short_integer(scalar_type) ? ".half"\n                    : minic_type_is_long_integer(scalar_type)  ? ".dword"\n                                                               : ".word";\n''',
    '''        directive = minic_riscv64_integer_data_directive(scalar_width);\n        if (directive == NULL) {\n            return false;\n        }\n''',
    "rv64-global-directive-width",
)
text = replace_once(
    text,
    '''            if (minic_type_is_char_integer(scalar_type)) {\n''',
    '''            if (scalar_width == 1U) {\n''',
    "rv64-global-byte-format",
)
path.write_text(text)

path = root / "tests/compiler/c0/static_prefix_object_attributes.c"
path.write_text('''typedef _Bool bool;\n\nenum {\n    false = 0,\n    true = 1\n};\n\nstatic __attribute__((__unused__)) const bool class_irq_is_conditional = false;\n\nstatic inline __attribute__((__always_inline__)) int class_irq_add(int value) {\n    return value + 1;\n}\n\nint main(void) {\n    return class_irq_is_conditional == false && class_irq_add(6) == 7 ? 0 : 1;\n}\n''')

path = root / "tests/compiler/c0/run-static-prefix-object-attributes.sh"
text = path.read_text()
text = replace_once(
    text,
    "grep -F 'class_irq_is_conditional:' \"$assembly\" >/dev/null\n",
    "grep -F 'class_irq_is_conditional:' \"$assembly\" >/dev/null\ngrep -F '  .byte 0' \"$assembly\" >/dev/null\ngrep -F '.size class_irq_is_conditional, 1' \"$assembly\" >/dev/null\n",
    "static-global-bool-width-gate",
)
text = replace_once(
    text,
    "prefix=unused object=static-const function=static-inline single-pass-head=1 semantic-probe=none",
    "prefix=unused object=static-const-bool initializer=shared-ice-enum-false width=1-byte function=static-inline single-pass-head=1 semantic-probe=none",
    "static-global-ice-summary",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-array-bound-constant-expressions.sh"
text = path.read_text()
text = replace_once(
    text,
    "expected integer constant expression in array bound",
    "expected integer constant expression",
    "array-bound-shared-ice-diagnostic",
)
path.write_text(text)

path = root / "tests/compiler/c0/run.sh"
text = path.read_text()
text = replace_once(
    text,
    '''expect_compile_failure \\\n    invalid_braced_scalar_static_global \\\n    "expected integer or character constant"''',
    '''expect_compile_failure \\\n    invalid_braced_scalar_static_global \\\n    "expected integer constant expression"''',
    "static-global-braced-shared-ice-diagnostic",
)
path.write_text(text)
