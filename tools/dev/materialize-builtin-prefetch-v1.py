from pathlib import Path

root = Path(__file__).resolve().parents[2]

parser_path = root / "src/frontend/parser_expression.c"
text = parser_path.read_text()
anchor = '''static bool current_is_builtin_offsetof(const MinicParser *parser) {
'''
implementation = r'''static bool parse_builtin_prefetch(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression discard_cast;
    MinicExpressionId address_id;
    const MinicExpression *address;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t rw = 0;
    int64_t locality = 3;

    if (parser == NULL || expression_id == NULL ||
        !current_identifier_is(parser, "__builtin_prefetch")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_prefetch") ||
        !parse_expression_internal(parser, &address_id, 0U, true)) {
        return false;
    }
    address = minic_c0_program_expression(parser->program, address_id);
    if (address == NULL || !minic_type_is_pointer(address->type)) {
        minic_parser_error(parser, "__builtin_prefetch address must have pointer type");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_integer_constant_expression(parser, &rw)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "__builtin_prefetch rw must be an integer constant");
            }
            return false;
        }
        if (rw < 0 || rw > 2) {
            minic_parser_error(parser, "__builtin_prefetch rw must be between 0 and 2");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_integer_constant_expression(parser, &locality)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "__builtin_prefetch locality must be an integer constant");
                }
                return false;
            }
            if (locality < 0 || locality > 3) {
                minic_parser_error(parser, "__builtin_prefetch locality must be between 0 and 3");
                return false;
            }
        }
    }
    (void)rw;
    (void)locality;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_prefetch arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* Prefetch is an optimization hint. Keep the address expression as a real
     * runtime edge so side effects and address evaluation are preserved, but
     * lower the hint itself to a void cast. Cast normalization turns this into
     * the canonical DISCARD node; targets may therefore omit a prefetch
     * instruction without inventing a prefetch-specific AST representation. */
    (void)memset(&discard_cast, 0, sizeof(discard_cast));
    discard_cast.kind = MINIC_EXPRESSION_CAST;
    discard_cast.span.begin = begin;
    discard_cast.span.end = end;
    discard_cast.type = minic_type_void();
    discard_cast.value_category = MINIC_VALUE_RVALUE;
    discard_cast.value.unary.operand = address_id;
    return minic_parser_add_expression(parser, &discard_cast, expression_id);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("builtin insertion anchor changed")
text = text.replace(anchor, implementation + anchor)

expect_block = r'''    if (current_identifier_is(parser, "__builtin_expect")) {
        if (!parse_builtin_expect(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
prefetch_block = expect_block + r'''    if (current_identifier_is(parser, "__builtin_prefetch")) {
        if (!parse_builtin_prefetch(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
if text.count(expect_block) != 1:
    raise SystemExit("builtin primary dispatch anchor changed")
parser_path.write_text(text.replace(expect_block, prefetch_block))

(root / "tests/compiler/c0/builtin_prefetch.c").write_text(r'''int main(void)
{
    int values[4];
    int *cursor = values;

    __builtin_prefetch(cursor);
    __builtin_prefetch(cursor++, 1);
    __builtin_prefetch(cursor++, 2, 0);
    return cursor == values + 2 ? 0 : 1;
}
''')
(root / "tests/compiler/c0/invalid_builtin_prefetch_rw.c").write_text(r'''int main(void)
{
    int value = 0;
    __builtin_prefetch(&value, 3);
    return 0;
}
''')
(root / "tests/compiler/c0/invalid_builtin_prefetch_locality.c").write_text(r'''int main(void)
{
    int value = 0;
    __builtin_prefetch(&value, 0, 4);
    return 0;
}
''')
(root / "tests/compiler/c0/invalid_builtin_prefetch_address.c").write_text(r'''int main(void)
{
    __builtin_prefetch(7);
    return 0;
}
''')
(root / "tests/compiler/c0/run-builtin-prefetch.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
target_cc=${TARGET_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-prefetch

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_prefetch.c" -o "$work/builtin_prefetch.i"
"$minic" -S "$work/builtin_prefetch.i" -o "$work/builtin_prefetch.s"
if grep -F '__builtin_prefetch' "$work/builtin_prefetch.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/builtin_prefetch: runtime builtin symbol leaked' >&2
    exit 1
fi
"$target_cc" -static "$work/builtin_prefetch.s" -o "$work/builtin_prefetch"
"$qemu" "$work/builtin_prefetch"

check_invalid() {
    name=$1
    message=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" >"$work/$name.out" 2>"$work/$name.err"; then
        printf 'FAIL compiler/c0/%s: compilation unexpectedly succeeded\n' "$name" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.err" >/dev/null
}

check_invalid invalid_builtin_prefetch_rw '__builtin_prefetch rw must be between 0 and 2'
check_invalid invalid_builtin_prefetch_locality '__builtin_prefetch locality must be between 0 and 3'
check_invalid invalid_builtin_prefetch_address '__builtin_prefetch address must have pointer type'

printf '%s\n' 'PASS compiler/c0/builtin_prefetch arity=1,2,3 address=pointer side-effects=preserved rw=0..2 locality=0..3 rv64-hint=optional'
''')

foundation = root / "tests/compiler/c0/run-foundation-focused.sh"
text = foundation.read_text()
needle = '''    run-builtin-expect.sh \\
'''
replacement = needle + '''    run-builtin-prefetch.sh \\
'''
if text.count(needle) != 1:
    raise SystemExit("foundation focused builtin anchor changed")
foundation.write_text(text.replace(needle, replacement))
