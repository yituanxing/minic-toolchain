from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_statement.c"
focused_path = root / "tools/dev/pr76-focused.sh"
fixture_path = root / "tests/compiler/c0/gnu_auto_type_local.c"
runner_path = root / "tests/compiler/c0/run-gnu-auto-type-local.sh"

parser = parser_path.read_text()
anchor = '''static bool parse_declaration(MinicParser *parser) {
    MinicType base_type;
    bool is_register_storage;
'''
helper = r'''static bool current_identifier_is_auto_type(const MinicParser *parser) {
    return parser != NULL && parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           identifier_equals(parser, parser->current.span, "__auto_type", 11U);
}

static bool parse_auto_type_local_declaration(MinicParser *parser) {
    const MinicExpression *initializer;
    MinicExpressionId initializer_id;
    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicSourcePosition begin;

    if (!current_identifier_is_auto_type(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declarator must be an identifier");
        }
        return false;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span = parser->current.span;
    local.element_count = 1U;
    local.storage_offset = 0U;
    local.is_array = false;
    local.is_register_storage = false;
    if (minic_parser_name_bound_in_current_scope(parser, local.name_span)) {
        minic_parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_EQUAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "GNU __auto_type declaration requires an initializer");
        }
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &initializer_id, 0U)) {
        return false;
    }
    initializer = minic_c0_program_expression(parser->program, initializer_id);
    if (initializer == NULL || minic_type_is_void(initializer->type) ||
        !minic_parser_require_complete_object_type(
            parser,
            initializer->type,
            "GNU __auto_type initializer must determine a complete object type")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "invalid GNU __auto_type initializer type");
        }
        return false;
    }
    local.type = initializer->type;

    /* GNU __auto_type deliberately keeps the new name out of scope while the
       initializer is parsed.  Bind it only after the initializer type is known. */
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !minic_parser_bind_local(parser, local.name_span, local_id) ||
        !add_local_lvalue_expression(parser, local_id, local.name_span, &target_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot materialize GNU __auto_type local");
        }
        return false;
    }

    if (minic_type_is_record(local.type)) {
        if (!minic_c0_record_value_is_address_backed(parser->program, initializer_id) ||
            !add_record_copy_assignments(parser, target_id, initializer_id, initializer->span)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "GNU __auto_type record initializer must be address-backed");
            }
            return false;
        }
    } else {
        MinicStatement statement;

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = begin;
        statement.span.end = initializer->span.end;
        statement.target_expression = target_id;
        statement.expression = initializer_id;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_assignment_compatible(parser->program, local.type, initializer_id) ||
            !minic_parser_add_statement(parser, &statement)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize GNU __auto_type local");
            }
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU __auto_type declaration");
}

'''
if parser.count(anchor) != 1:
    raise SystemExit("declaration helper anchor mismatch")
parser = parser.replace(anchor, helper + anchor, 1)

dispatch_anchor = '''    if (token_starts_local_declaration(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return parse_declaration(parser);
    }
'''
dispatch = '''    if (current_identifier_is_auto_type(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "GNU __auto_type requires a compound statement scope");
            return false;
        }
        return parse_auto_type_local_declaration(parser);
    }
''' + dispatch_anchor
if parser.count(dispatch_anchor) != 1:
    raise SystemExit("local declaration dispatch anchor mismatch")
parser_path.write_text(parser.replace(dispatch_anchor, dispatch, 1))

fixture_path.write_text(r'''static long linux_max_shape(long delta)
{
    return ({
        __auto_type x = (0L);
        __auto_type y = (delta);
        typeof(x) check = x + y;
        (void)check;
        x > y ? x : y;
    });
}

static long initializer_scope(long x)
{
    {
        __auto_type x = x + 1;
        return x;
    }
}

static int pointer_inference(void)
{
    int value = 0;
    __auto_type pointer = &value;
    *pointer = 7;
    return value;
}

int main(void)
{
    return linux_max_shape(-3) == 0 && linux_max_shape(9) == 9 &&
                   initializer_scope(4) == 5 && pointer_inference() == 7
               ? 0
               : 1;
}
''')

runner_path.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-gnu-auto-type-local"

mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_auto_type_local.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F "linux_max_shape:" "$work/output.s" >/dev/null
grep -F "initializer_scope:" "$work/output.s" >/dev/null
grep -F "pointer_inference:" "$work/output.s" >/dev/null

cat >"$work/missing-initializer.c" <<'EOF'
int f(void) { __auto_type value; return 0; }
EOF
"$host_cc" -E -P -x c "$work/missing-initializer.c" -o "$work/missing-initializer.i"
if "$minic" -S "$work/missing-initializer.i" -o "$work/missing-initializer.s" 2>"$work/missing-initializer.stderr"; then
    printf '%s\n' "FAIL compiler/c0/gnu_auto_type_local: missing initializer accepted" >&2
    exit 1
fi
grep -F "GNU __auto_type declaration requires an initializer" "$work/missing-initializer.stderr" >/dev/null

cat >"$work/multiple.c" <<'EOF'
int f(void) { __auto_type first = 1, second = 2; return first + second; }
EOF
"$host_cc" -E -P -x c "$work/multiple.c" -o "$work/multiple.i"
if "$minic" -S "$work/multiple.i" -o "$work/multiple.s" 2>"$work/multiple.stderr"; then
    printf '%s\n' "FAIL compiler/c0/gnu_auto_type_local: multiple declarators accepted" >&2
    exit 1
fi
grep -F "expected ';' after GNU __auto_type declaration" "$work/multiple.stderr" >/dev/null

printf '%s\n' "PASS compiler/c0/gnu_auto_type_local inference=initializer scope=after-initializer linux-max-shape=1 pointer=1 single-declarator=1 initialized=1"
''')
runner_path.chmod(0o755)

focused = focused_path.read_text()
registration = "sh tests/compiler/c0/run-gnu-object-alignment-attribute.sh\n"
if focused.count(registration) != 1:
    raise SystemExit("focused registration anchor mismatch")
focused_path.write_text(
    focused.replace(registration,
                    registration + "sh tests/compiler/c0/run-gnu-auto-type-local.sh\n",
                    1)
)

print("PASS generated GNU __auto_type local slice")
