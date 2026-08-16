#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


parser_path = "src/frontend/parser_expression.c"
marker = "static bool parse_builtin_expect(MinicParser *parser, MinicExpressionId *expression_id) {\n"
helper = r'''static bool function_name_equals_literal(const MinicFunction *function, const char *name) {
    size_t length;

    if (function == NULL || name == NULL) {
        return false;
    }
    length = strlen(name);
    return function->name_length == length && memcmp(function->name, name, length) == 0;
}

static bool ensure_builtin_memcpy_callee(MinicParser *parser, MinicFunctionId *function_id) {
    static const char memcpy_name[] = "memcpy";
    MinicType void_type;
    MinicType const_void_type;
    MinicType void_pointer_type;
    MinicType const_void_pointer_type;
    MinicType parameter_types[3];
    size_t index;

    if (parser == NULL || parser->program == NULL || function_id == NULL) {
        return false;
    }
    void_type = minic_type_void();
    if (!minic_type_add_const(void_type, &const_void_type) ||
        !minic_type_pointer_to(void_type, &void_pointer_type) ||
        !minic_type_pointer_to(const_void_type, &const_void_pointer_type)) {
        minic_parser_error(parser, "cannot form __builtin_memcpy signature");
        return false;
    }
    parameter_types[0] = void_pointer_type;
    parameter_types[1] = const_void_pointer_type;
    parameter_types[2] = minic_type_unsigned_long();

    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        function = minic_c0_program_function(parser->program, index);
        if (!function_name_equals_literal(function, memcpy_name)) {
            continue;
        }
        if (function->is_variadic || function->parameter_count != 3U ||
            !minic_type_equal(function->return_type, void_pointer_type) ||
            !minic_type_equal(function->parameter_types[0], parameter_types[0]) ||
            !minic_type_equal(function->parameter_types[1], parameter_types[1]) ||
            !minic_type_equal(function->parameter_types[2], parameter_types[2])) {
            minic_parser_error(parser, "__builtin_memcpy conflicts with memcpy declaration");
            return false;
        }
        *function_id = index;
        return true;
    }

    if (!minic_c0_program_add_function(parser->program,
                                       memcpy_name,
                                       sizeof(memcpy_name) - 1U,
                                       parser->program->local_count,
                                       0U,
                                       MINIC_BLOCK_INVALID,
                                       function_id) ||
        !minic_c0_program_set_function_signature(
            parser->program, *function_id, void_pointer_type, parameter_types, 3U)) {
        minic_parser_error(parser, "cannot create canonical memcpy builtin callee");
        return false;
    }
    return true;
}

static bool parse_builtin_memcpy(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression call_expression;
    MinicFunctionId function_id;
    const MinicFunction *callee;
    MinicSourcePosition begin;
    MinicSourcePosition end;

    if (parser == NULL || expression_id == NULL ||
        !current_identifier_is(parser, "__builtin_memcpy")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!ensure_builtin_memcpy_callee(parser, &function_id) ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_memcpy")) {
        return false;
    }
    callee = minic_c0_program_function(parser->program, function_id);
    if (callee == NULL) {
        minic_parser_error(parser, "invalid canonical memcpy builtin callee");
        return false;
    }

    (void)memset(&call_expression, 0, sizeof(call_expression));
    call_expression.kind = MINIC_EXPRESSION_CALL;
    call_expression.span.begin = begin;
    call_expression.type = callee->return_type;
    call_expression.value_category = MINIC_VALUE_RVALUE;
    call_expression.value.call.function_id = function_id;
    if (!parse_call_arguments(parser, &call_expression, callee)) {
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    call_expression.span.end = end;
    return minic_parser_add_expression(parser, &call_expression, expression_id);
}

'''
replace_once(parser_path, marker, helper + marker, "builtin memcpy semantic helper")

dispatch = '''    if (current_identifier_is(parser, "__builtin_expect")) {\n'''
new_dispatch = '''    if (current_identifier_is(parser, "__builtin_memcpy")) {\n        if (!parse_builtin_memcpy(parser, &primary_id) ||\n            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {\n            return false;\n        }\n        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n''' + dispatch
replace_once(parser_path, dispatch, new_dispatch, "builtin memcpy primary dispatch")

Path("tests/compiler/c0/builtin_memcpy_call.c").write_text(
    r'''void *copy_with_builtin(char *dest, const char *src, unsigned long length) {
    return __builtin_memcpy(dest, src, length);
}

void *memcpy(void *dest, const void *src, unsigned long length);

void copy_and_discard(char *dest, const char *src, unsigned long length) {
    (void)__builtin_memcpy(dest, src, length);
}
'''
)

Path("tests/compiler/c0/run-builtin-memcpy-call.sh").write_text(
    r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/builtin-memcpy-call

mkdir -p "$work"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/builtin_memcpy_call.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

test "$(grep -c -F '  call memcpy' "$work/output.s")" -eq 2
if grep -F '__builtin_memcpy' "$work/output.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/builtin_memcpy_call: builtin spelling leaked to assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/builtin_memcpy_call canonical CALL memcpy'
'''
)

focused = Path("tests/compiler/c0/run-foundation-focused.sh")
text = focused.read_text()
anchor = "    run-builtin-expect.sh \\\n"
insert = anchor + "    run-builtin-memcpy-call.sh \\\n"
if text.count(anchor) != 1:
    raise SystemExit(f"foundation builtin anchor: expected 1 match, found {text.count(anchor)}")
focused.write_text(text.replace(anchor, insert, 1))

print("staged __builtin_memcpy -> canonical memcpy CALL lowering")
