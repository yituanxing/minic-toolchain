from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

replace_once(
    'src/frontend/const_eval.h',
    '''bool minic_const_eval_integer(const MinicC0Program *program,\n                              const MinicTargetInfo *target,\n                              MinicExpressionId expression_id,\n                              MinicConstValue *value);\n''',
    '''bool minic_const_eval_integer(const MinicC0Program *program,\n                              const MinicTargetInfo *target,\n                              MinicExpressionId expression_id,\n                              MinicConstValue *value);\nbool minic_const_value_convert_integer(const MinicC0Program *program,\n                                       const MinicTargetInfo *target,\n                                       const MinicConstValue *source,\n                                       MinicType type,\n                                       MinicConstValue *result);\n''',
    'const-eval public conversion declaration')

replace_once(
    'src/frontend/const_eval.c',
    '''    result->type = type;\n    return normalize_bits(program, target, type, bits, &result->bits);\n}\n\nstatic bool value_truthy''',
    '''    result->type = type;\n    return normalize_bits(program, target, type, bits, &result->bits);\n}\n\nbool minic_const_value_convert_integer(const MinicC0Program *program,\n                                       const MinicTargetInfo *target,\n                                       const MinicConstValue *source,\n                                       MinicType type,\n                                       MinicConstValue *result) {\n    return convert_value(program, target, source, type, result);\n}\n\nstatic bool value_truthy''',
    'const-eval public conversion implementation')

old_helper = '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,\n                                                  MinicType target_type,\n                                                  int *value) {\n    MinicConstValue constant;\n    MinicExpressionId expression_id;\n    int64_t signed_value;\n\n    if (parser == NULL || value == NULL || !minic_type_is_integer(target_type)) {\n        if (parser != NULL) {\n            minic_parser_error(parser, "integer initializer requires an integer target type");\n        }\n        return false;\n    }\n    if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {\n        return false;\n    }\n    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {\n        minic_parser_error(parser, "integer initializer type mismatch");\n        return false;\n    }\n    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {\n        minic_parser_error(parser, "integer initializer requires an integer constant expression");\n        return false;\n    }\n    if (!minic_const_value_as_int64(\n            parser->program, parser->target_info, &constant, &signed_value) ||\n        signed_value < INT_MIN || signed_value > INT_MAX) {\n        minic_parser_error(parser, "integer initializer exceeds current global payload range");\n        return false;\n    }\n    *value = (int)signed_value;\n    return true;\n}\n'''
new_helper = '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,\n                                                  MinicType target_type,\n                                                  int *value) {\n    MinicConstValue constant;\n    MinicConstValue converted;\n    MinicExpressionId expression_id;\n    int64_t signed_value;\n    unsigned int width;\n\n    if (parser == NULL || value == NULL || !minic_type_is_integer(target_type)) {\n        if (parser != NULL) {\n            minic_parser_error(parser, "integer initializer requires an integer target type");\n        }\n        return false;\n    }\n    if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {\n        return false;\n    }\n    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {\n        minic_parser_error(parser, "integer initializer type mismatch");\n        return false;\n    }\n    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {\n        minic_parser_error(parser, "integer initializer requires an integer constant expression");\n        return false;\n    }\n    if (!minic_const_value_convert_integer(\n            parser->program, parser->target_info, &constant, target_type, &converted) ||\n        !minic_target_info_integer_width(\n            parser->target_info, parser->program, target_type, &width) ||\n        width == 0U || width > 64U) {\n        minic_parser_error(parser, "cannot convert integer initializer to target type");\n        return false;\n    }\n    if (width <= 32U && minic_type_is_unsigned_integer(target_type)) {\n        uint32_t raw;\n\n        _Static_assert(sizeof(int) == sizeof(uint32_t),\n                       "MiniC global initializer payload requires 32-bit host int");\n        raw = (uint32_t)converted.bits;\n        if (width < 32U) {\n            raw &= (UINT32_C(1) << width) - UINT32_C(1);\n            *value = (int)raw;\n        } else {\n            (void)memcpy(value, &raw, sizeof(raw));\n        }\n        return true;\n    }\n    if (!minic_const_value_as_int64(\n            parser->program, parser->target_info, &converted, &signed_value) ||\n        signed_value < INT_MIN || signed_value > INT_MAX) {\n        minic_parser_error(parser, "integer initializer exceeds current global payload range");\n        return false;\n    }\n    *value = (int)signed_value;\n    return true;\n}\n'''
replace_once('src/frontend/parser_core.c', old_helper, new_helper, 'typed initializer payload helper')

replace_once(
    'src/frontend/parser_global.c',
    '''    if (minic_type_is_integer(type)) {\n        int64_t parsed;\n\n        if (!minic_parser_parse_integer_constant_expression(parser, &parsed) || parsed < INT_MIN ||\n            parsed > INT_MAX ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, (int)parsed)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "static aggregate integer initializer is out of range");\n            }\n            return false;\n        }\n''',
    '''    if (minic_type_is_integer(type)) {\n        int parsed;\n\n        if (!minic_parser_parse_integer_initializer_value(parser, type, &parsed) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, parsed)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static aggregate integer initializer");\n            }\n            return false;\n        }\n''',
    'nested static scalar typed initializer')

test = root / 'tests/compiler/c0/static_record_compound_literal.c'
test.write_text(r'''typedef struct Inner {
    int first;
    unsigned int magic;
    int second;
} Inner;

typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

static Outer value = { 3, (Inner) { .magic = 0xdead4ead, .second = 7 } };

int main(void)
{
    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&
                   value.inner.second == 7
               ? 0
               : 1;
}
''')

script = root / 'tests/compiler/c0/run-static-aggregate-family-discovery.sh'
text = script.read_text()
old = '''"$minic" -S "$root/tests/compiler/c0/static_record_compound_literal.c" \\
    -o "$build_dir/static_record_compound_literal.s"\n'''
new = old + '''grep -F '.word -559067475' "$build_dir/static_record_compound_literal.s" >/dev/null\n'''
if text.count(old) != 1:
    raise SystemExit(f'focused unsigned bit-pattern anchor mismatch: {text.count(old)}')
script.write_text(text.replace(old, new, 1))
