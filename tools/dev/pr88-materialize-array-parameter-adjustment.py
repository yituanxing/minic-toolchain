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
    '''static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer parameters are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, parameter_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    *name_span = declarator.name_span;
    *has_name = declarator.has_name;
    return true;
}
''',
    '''static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer parameters are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, parameter_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    *name_span = declarator.name_span;
    *has_name = declarator.has_name;
    return true;
}

static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType declared_array_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
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
    "shared array parameter adjustment helper",
)

replace_once(
    "src/frontend/parser_function.c",
    '''        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
''',
    '''        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        if (!is_function_pointer_parameter && parser->current.kind == MINIC_TOKEN_LBRACKET &&
            !adjust_array_parameter_type(parser, &parameter_type)) {
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
''',
    "apply array parameter adjustment before signature storage",
)

Path("tests/compiler/c0/array_parameter_adjustment.c").write_text(
    '''typedef unsigned char u8;

void generate_random_uuid(u8 uuid[16]);
void generate_random_uuid(u8 *uuid)
{
    uuid[0] = 7;
}

void unnamed_array_parameter(u8 [1 << 4]);
void unnamed_array_parameter(u8 *bytes)
{
    bytes[1] = 9;
}

typedef void (*generator_fn)(u8 bytes[16]);

static int adjusted_size(u8 bytes[16])
{
    _Static_assert(sizeof(bytes) == sizeof(void *), "array parameter adjusts to pointer");
    return (int)bytes[0];
}

int main(void)
{
    u8 bytes[16] = {0};
    generator_fn fn = generate_random_uuid;
    fn(bytes);
    unnamed_array_parameter(bytes);
    return adjusted_size(bytes) == 7 && bytes[1] == 9 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-array-parameter-adjustment.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/array_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'generate_random_uuid:' "$work/output.s" >/dev/null
grep -F 'unnamed_array_parameter:' "$work/output.s" >/dev/null
grep -F 'adjusted_size:' "$work/output.s" >/dev/null

cat >"$work/static-bound.c" <<'EOF'
void unsupported(int values[static 4]);
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/static-bound.c" -o "$work/static-bound.i"
if "$minic" -S "$work/static-bound.i" -o "$work/static-bound.s" 2>"$work/static-bound.stderr"; then
    printf '%s\n' 'parameter [static N] unexpectedly accepted by bounded v0' >&2
    exit 1
fi
test -s "$work/static-bound.stderr"

printf '%s\n' \
  'PASS compiler/c0/array_parameter_adjustment named=1 unnamed=1 fixed-bound=discarded function-type=pointer redeclaration=array-pointer-compatible function-pointer-typedef=1 sizeof=pointer static-bound=fail-closed'
'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-array-parameter-adjustment.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
