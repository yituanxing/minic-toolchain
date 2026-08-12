from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


path = "src/frontend/parser_statement.c"
p = Path(path)
text = p.read_text()
start = text.find("static bool case_integer_constant_value(")
end = text.find("static bool parse_case(", start)
if start < 0 or end < 0:
    raise SystemExit(f"case evaluator region mismatch start={start} end={end}")
replacement = '''static bool case_integer_constant_value(const MinicParser *parser,
                                        MinicExpressionId expression_id,
                                        int64_t *value) {
    MinicConstValue constant;

    if (parser == NULL || parser->program == NULL || parser->target_info == NULL || value == NULL ||
        !minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {
        return false;
    }
    return minic_const_value_as_int64(parser->program, parser->target_info, &constant, value);
}

'''
text = text[:start] + replacement + text[end:]
text = text.replace(
    "!case_integer_constant_value(parser->program, lower_expression_id, &lower_value)",
    "!case_integer_constant_value(parser, lower_expression_id, &lower_value)",
)
text = text.replace(
    "!case_integer_constant_value(parser->program, upper_expression_id, &upper_value)",
    "!case_integer_constant_value(parser, upper_expression_id, &upper_value)",
)
p.write_text(text)

Path("tests/compiler/c0/switch_typed_consteval.c").write_text(r'''typedef unsigned char u8;
typedef u8 blk_status_t;

int linux_casted_case(blk_status_t error)
{
    switch (error) {
    case ((blk_status_t)1):
        return 1;
    case ((blk_status_t)(1 + 2)):
        return 3;
    case ((blk_status_t)4) ... ((blk_status_t)(2 + 4)):
        return 4;
    default:
        return 0;
    }
}

int typed_narrowing_case(unsigned int value)
{
    switch (value) {
    case ((u8)257):
        return 1;
    case ((u8)-1):
        return 255;
    default:
        return 0;
    }
}
''')

script = Path("tests/compiler/c0/run-switch-control-flow.sh")
text = script.read_text()
needle = '''printf '%s\\n' "PASS compiler/c0/switch_control_flow_lowering"\n'''
addition = '''printf '%s\\n' "PASS compiler/c0/switch_control_flow_lowering"\n\n"$host_cc" -E -P -std=gnu11 -x c \\
    "$root/tests/compiler/c0/switch_typed_consteval.c" \\
    -o "$work/switch_typed_consteval.i"\n"$minic" -S \\
    "$work/switch_typed_consteval.i" \\
    -o "$work/switch_typed_consteval.s"\ngrep -F 'linux_casted_case:' "$work/switch_typed_consteval.s" >/dev/null\ngrep -F 'typed_narrowing_case:' "$work/switch_typed_consteval.s" >/dev/null\nprintf '%s\\n' "PASS compiler/c0/switch_typed_consteval casted-typedef=1 binary=1 range=1 narrowing=target-aware"\n'''
if text.count(needle) != 1:
    raise SystemExit(f"switch focused anchor mismatch: {text.count(needle)}")
script.write_text(text.replace(needle, addition, 1))
