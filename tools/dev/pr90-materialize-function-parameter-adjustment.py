#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_function.c",
    '''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType declared_array_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot parse array parameter declarator");
        }
        return false;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL || !minic_type_pointer_to(outer_array->element_type, parameter_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    return true;
}
''',
    '''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType declared_array_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot parse array parameter declarator");
        }
        return false;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL || !minic_type_pointer_to(outer_array->element_type, parameter_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    return true;
}

static bool adjust_function_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    MinicType adjusted_type;

    if (parser == NULL || parameter_type == NULL) {
        return false;
    }
    if (!minic_type_is_function(*parameter_type)) {
        return true;
    }
    if (!minic_type_pointer_to(*parameter_type, &adjusted_type)) {
        minic_parser_error(parser, "cannot adjust function parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;
    return true;
}
''',
    "function parameter adjustment helper",
)

replace_once(
    "src/frontend/parser_function.c",
    '''        if (!is_function_pointer_parameter && parser->current.kind == MINIC_TOKEN_LBRACKET &&
            !adjust_array_parameter_type(parser, &parameter_type)) {
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
''',
    '''        if (!is_function_pointer_parameter && parser->current.kind == MINIC_TOKEN_LBRACKET &&
            !adjust_array_parameter_type(parser, &parameter_type)) {
            return false;
        }
        if (!is_function_pointer_parameter &&
            !adjust_function_parameter_type(parser, &parameter_type)) {
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
''',
    "apply function parameter adjustment before signature storage",
)

Path("tests/compiler/c0/function_parameter_adjustment.c").write_text(
    '''typedef int callback_t(int value);

typedef int done_t(int value, void *private_data);

int apply_callback(callback_t callback);
int apply_callback(int (*callback)(int value))
{
    _Static_assert(sizeof(callback) == sizeof(void *), "function parameter adjusts to pointer");
    return callback(7);
}

int invoke_done(done_t done, void *private_data)
{
    return done(5, private_data);
}

int plus_one(int value)
{
    return value + 1;
}

int finish(int value, void *private_data)
{
    return value + (private_data != (void *)0);
}

int main(void)
{
    int marker = 1;
    return apply_callback(plus_one) == 8 && invoke_done(finish, &marker) == 6 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-function-parameter-adjustment.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/function_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'apply_callback:' "$work/output.s" >/dev/null
grep -F 'invoke_done:' "$work/output.s" >/dev/null
grep -F 'jalr' "$work/output.s" >/dev/null

printf '%s\n' \
  'PASS compiler/c0/function_parameter_adjustment typedef-function=pointer-adjusted declaration-pointer-redeclaration=compatible definition=1 indirect-call=1 sizeof=pointer'
'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-array-parameter-adjustment.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-function-parameter-adjustment.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
