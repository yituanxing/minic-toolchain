#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_range(text: str, start: str, end: str, new: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start anchor not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:begin] + new + text[finish:]


root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

# Replace the old AST-shape evaluator with one consumer of the shared parser-level ICE engine.
old_forward = '''static bool static_record_integer_constant(const MinicC0Program *program,\n                                           MinicExpressionId expression_id,\n                                           int *value);\n\n'''
helper = r'''static bool parse_static_local_integer_constant(MinicParser *parser,
                                                const char *range_message,
                                                int *value) {
    int64_t parsed;

    if (parser == NULL || range_message == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "%s", range_message);
        return false;
    }
    *value = (int)parsed;
    return true;
}

'''
text = replace_once(text, old_forward, helper, "static-local-ice-helper")

old_inferred = '''            } else {\n                MinicExpressionId value_id;\n                int value;\n\n                if (!minic_parser_parse_expression(parser, &value_id, 1U) ||\n                    !static_record_integer_constant(parser->program, value_id, &value) ||\n                    !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                        minic_parser_error(\n                            parser, "static local array requires integer constant initializers");\n                    }\n                    return false;\n                }\n            }\n'''
new_inferred = '''            } else {\n                int value;\n\n                if (!parse_static_local_integer_constant(\n                        parser,\n                        "static local array initializer is out of supported integer range",\n                        &value) ||\n                    !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                        minic_parser_error(\n                            parser, "static local array requires an integer constant expression");\n                    }\n                    return false;\n                }\n            }\n'''
text = replace_once(text, old_inferred, new_inferred, "inferred-static-local-shared-ice")

# Delete the duplicate literal/cast/unary AST evaluator entirely.
text = replace_range(
    text,
    "static bool static_record_integer_constant(const MinicC0Program *program,",
    "static bool parse_static_local_record_initializer",
    "",
    "remove-static-record-integer-evaluator",
)

old_record = '''        } else {\n            MinicExpressionId value_id;\n\n            if (!minic_type_is_integer(field->type) ||\n                !minic_parser_parse_expression(parser, &value_id, 1U) ||\n                !static_record_integer_constant(parser->program, value_id, &value)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(\n                        parser, "static record field requires an integer constant expression");\n                }\n                return false;\n            }\n        }\n'''
new_record = '''        } else {\n            if (!minic_type_is_integer(field->type) ||\n                !parse_static_local_integer_constant(\n                    parser, "static record field constant is out of supported integer range", &value)) {\n                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                    minic_parser_error(\n                        parser, "static record field requires an integer constant expression");\n                }\n                return false;\n            }\n        }\n'''
text = replace_once(text, old_record, new_record, "record-field-shared-ice")

old_scalar_decl = '''        MinicGlobalObjectId scalar_object_id;\n        MinicExpressionId scalar_initializer_id;\n        int scalar_value;\n        int scalar_symbol_length;\n'''
new_scalar_decl = '''        MinicGlobalObjectId scalar_object_id;\n        int scalar_value;\n        int scalar_symbol_length;\n'''
text = replace_once(text, old_scalar_decl, new_scalar_decl, "scalar-remove-expression-id")
old_scalar = '''        if (!minic_parser_parse_expression(parser, &scalar_initializer_id, 0U) ||\n            !static_record_integer_constant(\n                parser->program, scalar_initializer_id, &scalar_value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(\n                    parser, "static local integer requires a supported constant initializer");\n            }\n            return false;\n        }\n'''
new_scalar = '''        if (!parse_static_local_integer_constant(\n                parser, "static local integer constant is out of supported range", &scalar_value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(\n                    parser, "static local integer requires an integer constant expression");\n            }\n            return false;\n        }\n'''
text = replace_once(text, old_scalar, new_scalar, "scalar-shared-ice")
if "static_record_integer_constant" in text:
    raise SystemExit("duplicate static-local integer evaluator still referenced")
path.write_text(text)

# Extend the existing static-local regression to exercise the Linux 8245 shape and all migrated contexts.
path = root / "tests/compiler/c0/static_local_inferred_array.c"
path.write_text(r'''typedef unsigned char MiniByte;

typedef struct MiniRegs {
    unsigned long pad;
    unsigned int a0;
    unsigned int a1;
} MiniRegs;

typedef struct MiniPair {
    int first;
    int second;
} MiniPair;

int static_table_checksum(void) {
    static const MiniByte nextage[] = {1, 3, 3, 4, 4, 5, 6};
    static const unsigned int argument_offs[] = {
        __builtin_offsetof(MiniRegs, a0),
        __builtin_offsetof(MiniRegs, a1),
        4 + 12,
    };
    static const int scalar_offset = __builtin_offsetof(MiniRegs, a1);
    static const MiniPair pair = {
        __builtin_offsetof(MiniRegs, a0),
        1 + 2,
    };

    return (int)sizeof(nextage) + nextage[0] + nextage[6] +
           (int)argument_offs[0] + (int)argument_offs[1] + argument_offs[2] + scalar_offset +
           pair.first + pair.second;
}
''')

path = root / "tests/compiler/c0/run-static-local-inferred-arrays.sh"
text = path.read_text()
text = replace_once(
    text,
    '''grep -E '\\.size __minic_static_local_[^,]+, 7$' "$work/static_local_inferred_array.s" >/dev/null\n''',
    '''grep -E '\\.size __minic_static_local_[^,]+, 7$' "$work/static_local_inferred_array.s" >/dev/null\ngrep -F '  .word 8' "$work/static_local_inferred_array.s" >/dev/null\ngrep -F '  .word 12' "$work/static_local_inferred_array.s" >/dev/null\ngrep -F '  .word 16' "$work/static_local_inferred_array.s" >/dev/null\ngrep -F '  .word 3' "$work/static_local_inferred_array.s" >/dev/null\n''',
    "static-local-ice-assembly-checks",
)
text = replace_once(
    text,
    "element=uchar count=7 brace-constants=1 internal-rodata=1",
    "element=uchar count=7 shared-ice=offsetof,array,scalar,record arithmetic=1 internal-rodata=1",
    "static-local-ice-summary",
)
path.write_text(text)
