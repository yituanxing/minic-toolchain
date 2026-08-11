#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def read(p): return (ROOT / p).read_text()
def write(p, s): (ROOT / p).write_text(s)
def one(s, old, new, label):
    n=s.count(old)
    if n != 1: raise SystemExit(f"{label}: expected 1 match, found {n}")
    return s.replace(old,new,1)

# AST semantic leaf.
p="src/frontend/ast.h"; s=read(p)
s=one(s,
'''    MINIC_EXPRESSION_LABEL_ADDRESS,
    MINIC_EXPRESSION_CALL_FRAME_ADDRESS,
    MINIC_EXPRESSION_SIZEOF,
''',
'''    MINIC_EXPRESSION_LABEL_ADDRESS,
    MINIC_EXPRESSION_CALL_FRAME_ADDRESS,
    MINIC_EXPRESSION_BUILTIN_UNREACHABLE,
    MINIC_EXPRESSION_SIZEOF,
''',"ast kind")
write(p,s)

# Parser: exact zero-argument builtin, typed void semantic leaf.
p="src/frontend/parser_expression.c"; s=read(p)
anchor='''static bool parse_builtin_unary(MinicParser *parser,
                                MinicBuiltinUnaryOperator operator_kind,
'''
helper='''static bool parse_builtin_unreachable(MinicParser *parser,
                                      MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_unreachable")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_unreachable")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "__builtin_unreachable takes no arguments");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNREACHABLE;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

'''
s=one(s,anchor,helper+anchor,"parser helper")
anchor='''    if (generic_token_text_equals(parser, "__builtin_return_address")) {
'''
block='''    if (generic_token_text_equals(parser, "__builtin_unreachable")) {
        if (!parse_builtin_unreachable(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
'''
s=one(s,anchor,block+anchor,"primary dispatch")
write(p,s)

# Normalization: leaf survives unchanged.
p="src/frontend/cast_normalization.c"; s=read(p)
s=one(s,
'''    case MINIC_EXPRESSION_LABEL_ADDRESS:
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS:
    case MINIC_EXPRESSION_SIZEOF:
''',
'''    case MINIC_EXPRESSION_LABEL_ADDRESS:
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS:
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
    case MINIC_EXPRESSION_SIZEOF:
''',"normalization leaf")
write(p,s)

# Verifier: both Parsed and Normalized forms retain a void rvalue leaf.
p="src/frontend/ast_verifier.c"; s=read(p)
anchor='''    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS: {
'''
block='''    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
'''
s=one(s,anchor,block+anchor,"verifier leaf")
write(p,s)

# RV64: UB fact has no runtime operation in the non-optimizing backend.
p="src/target/riscv64/codegen_expression.c"; s=read(p)
anchor='''    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS: {
'''
block='''    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
        /* Reaching this expression is undefined by GNU C. Keep the semantic
         * leaf for future CFG/IR reasoning, but do not invent a target trap or
         * external runtime symbol in the current non-optimizing backend. */
        return minic_type_is_void(expression->type);
'''
s=one(s,anchor,block+anchor,"rv64 leaf")
write(p,s)

# Focused contract.
write("tests/compiler/c0/gnu_builtin_unreachable.c", r'''_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_unreachable()), void),
               "__builtin_unreachable must have void type");

void linux_bug_shape(int condition) {
    if (condition) {
        asm volatile("");
        __builtin_unreachable();
    }
}

int ordinary_path(int value) {
    if (value)
        return value;
    __builtin_unreachable();
}
''')
write("tests/compiler/c0/run-gnu-builtin-unreachable.sh", r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-builtin-unreachable
assembly="$work/gnu_builtin_unreachable.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_builtin_unreachable.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'linux_bug_shape:' "$assembly" >/dev/null
grep -F 'ordinary_path:' "$assembly" >/dev/null
if grep -F '__builtin_unreachable' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_builtin_unreachable: emitted runtime builtin symbol' >&2
    exit 1
fi

cat >"$work/argument.c" <<'EOF'
void invalid(void) { __builtin_unreachable(1); }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/argument.c" -o "$work/argument.i"
if "$minic" -S "$work/argument.i" -o "$work/argument.s" 2>"$work/argument.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_builtin_unreachable: argument accepted' >&2
    exit 1
fi
grep -F '__builtin_unreachable takes no arguments' "$work/argument.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_builtin_unreachable semantic-leaf=1 void-type=1 normalized-leaf=1 rv64-runtime-op=none args=zero-only'
''')

p="tools/dev/pr76-focused.sh"; s=read(p)
s=one(s,
'''sh tests/compiler/c0/run-gnu-call-frame-address.sh
''',
'''sh tests/compiler/c0/run-gnu-call-frame-address.sh
sh tests/compiler/c0/run-gnu-builtin-unreachable.sh
''',"focused registration")
write(p,s)
