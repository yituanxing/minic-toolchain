#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, content):
    (ROOT / path).write_text(content)


def one(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return content.replace(old, new, 1)


p = "src/frontend/ast.h"
s = read(p)
s = one(s,
'''    MINIC_EXPRESSION_CALL,
    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_UNARY,
''',
'''    MINIC_EXPRESSION_CALL,
    MINIC_EXPRESSION_COMPOUND_LITERAL,
    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_UNARY,
''', "expression kind")
s = one(s,
'''        struct {
            MinicBlockId block;
            MinicExpressionId result;
        } statement_expression;
''',
'''        struct {
            MinicLocalId local_id;
            MinicBlockId initializer_block;
        } compound_literal;
        struct {
            MinicBlockId block;
            MinicExpressionId result;
        } statement_expression;
''', "compound literal payload")
write(p, s)

p = "src/frontend/parser_internal.h"
s = read(p)
s = one(s,
'''bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);
bool minic_parser_parse_statement_expression(MinicParser *parser,
''',
'''bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);
bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,
                                                   MinicExpressionId target_id);
bool minic_parser_parse_statement_expression(MinicParser *parser,
''', "initializer service prototype")
write(p, s)

p = "src/frontend/parser_statement.c"
s = read(p)
s = one(s,
'''static bool parse_local_designated_record_initializer(MinicParser *parser,
                                                      MinicExpressionId target_id) {
''',
'''bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,
                                                   MinicExpressionId target_id) {
''', "initializer service definition")
s = one(s,
'''                return parse_local_designated_record_initializer(parser, target_id);
''',
'''                return minic_parser_parse_runtime_record_initializer(parser, target_id);
''', "local initializer consumer")
write(p, s)

p = "src/frontend/parser_expression.c"
s = read(p)
anchor = '''static bool parse_cast(MinicParser *parser, MinicExpressionId *expression_id) {
'''
helper = r'''static bool parse_record_compound_literal(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicType type,
                                         MinicExpressionId *expression_id) {
    const MinicRecord *record;
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression hidden_lvalue;
    MinicExpression compound_literal;
    MinicExpressionId hidden_lvalue_id;
    MinicExpressionId compound_literal_id;
    MinicBlockId initializer_block;
    MinicBlockId parent_block;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE ||
        parser->current_function == MINIC_FUNCTION_INVALID || !minic_type_is_record(type)) {
        if (parser != NULL) {
            minic_parser_error(parser,
                               "compound literals currently require a block-scope record type");
        }
        return false;
    }
    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "record compound literal requires a complete record type");
        return false;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span.begin = begin;
    local.name_span.end = begin;
    local.type = type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        minic_parser_error(parser, "cannot allocate compound literal backing object");
        return false;
    }

    (void)memset(&hidden_lvalue, 0, sizeof(hidden_lvalue));
    hidden_lvalue.kind = MINIC_EXPRESSION_LOCAL;
    hidden_lvalue.span.begin = begin;
    hidden_lvalue.span.end = parser->current.span.begin;
    hidden_lvalue.type = type;
    hidden_lvalue.value_category = MINIC_VALUE_LVALUE;
    hidden_lvalue.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &hidden_lvalue, &hidden_lvalue_id) ||
        !minic_c0_program_add_block(parser->program, &initializer_block)) {
        minic_parser_error(parser, "cannot create compound literal initializer block");
        return false;
    }

    parent_block = parser->current_block;
    parser->current_block = initializer_block;
    success = minic_parser_parse_runtime_record_initializer(parser, hidden_lvalue_id);
    parser->current_block = parent_block;
    if (!success) {
        return false;
    }

    (void)memset(&compound_literal, 0, sizeof(compound_literal));
    compound_literal.kind = MINIC_EXPRESSION_COMPOUND_LITERAL;
    compound_literal.span.begin = begin;
    compound_literal.span.end = parser->current.span.begin;
    compound_literal.type = type;
    compound_literal.value_category = MINIC_VALUE_LVALUE;
    compound_literal.value.compound_literal.local_id = local_id;
    compound_literal.value.compound_literal.initializer_block = initializer_block;
    if (!minic_parser_add_expression(parser, &compound_literal, &compound_literal_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, compound_literal_id, expression_id);
}

'''
s = one(s, anchor, helper + anchor, "compound literal parser helper")
s = one(s,
'''    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type") ||
        !parse_unary(parser, &operand_id, true)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
''',
'''    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return parse_record_compound_literal(parser, begin, target_type, expression_id);
    }
    if (!parse_unary(parser, &operand_id, true)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
''', "cast dispatch")
write(p, s)

p = "src/frontend/cast_normalization.c"
s = read(p)
s = one(s,
'''    case MINIC_EXPRESSION_OFFSETOF:
        return true;
''',
'''    case MINIC_EXPRESSION_OFFSETOF:
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return true;
''', "normalization leaf")
write(p, s)

p = "src/frontend/ast_verifier.c"
s = read(p)
s = one(s,
'''    case MINIC_EXPRESSION_STATEMENT: {
''',
'''    case MINIC_EXPRESSION_COMPOUND_LITERAL: {
        const MinicLocal *local;
        const MinicBlock *initializer_block;
        const MinicRecord *record;

        local = minic_c0_program_local(program, expression->value.compound_literal.local_id);
        initializer_block = minic_c0_program_block(
            program, expression->value.compound_literal.initializer_block);
        record = minic_type_is_record(expression->type)
                     ? minic_c0_program_record(program, expression->type.record_id)
                     : NULL;
        return local != NULL && initializer_block != NULL && record != NULL && record->is_complete &&
               expression->value_category == MINIC_VALUE_LVALUE && !local->is_array &&
               !local->is_register_storage && local->element_count == 1U &&
               minic_type_equal(local->type, expression->type);
    }
    case MINIC_EXPRESSION_STATEMENT: {
''', "compound literal verifier")
write(p, s)

p = "src/target/riscv64/codegen_expression.c"
s = read(p)
anchor = '''bool minic_riscv64_emit_lvalue_address(FILE *file,
'''
helper = r'''static bool minic_riscv64_emit_expression_owned_block(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicExpressionId expression_id,
                                                       MinicBlockId block_id) {
    size_t label_stride;
    size_t label_counter;

    if (file == NULL || program == NULL || block_id >= program->block_count ||
        program->statement_count == SIZE_MAX) {
        return false;
    }
    label_stride = program->statement_count + 1U;
    if (expression_id > (SIZE_MAX - label_stride) / label_stride) {
        return false;
    }
    label_counter = label_stride + expression_id * label_stride;
    return minic_riscv64_emit_block(file, program, function, block_id, &label_counter);
}

'''
s = one(s, anchor, helper + anchor, "expression-owned block helper")
s = one(s,
'''    case MINIC_EXPRESSION_MEMBER:
        return minic_riscv64_emit_member_address(file, program, function, expression);
    default:
''',
'''    case MINIC_EXPRESSION_MEMBER:
        return minic_riscv64_emit_member_address(file, program, function, expression);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return minic_riscv64_emit_expression_owned_block(
                   file,
                   program,
                   function,
                   expression_id,
                   expression->value.compound_literal.initializer_block) &&
               minic_riscv64_emit_object_address(
                   file, program, function, expression->value.compound_literal.local_id);
    default:
''', "compound literal lvalue address")
s = one(s,
'''    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_load(file, program, function, expression->value.local_id);
''',
'''    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_load(file, program, function, expression->value.local_id);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return minic_riscv64_emit_lvalue_address(file, program, function, expression_id);
''', "compound literal expression emission")
s = one(s,
'''    case MINIC_EXPRESSION_STATEMENT: {
        size_t label_stride;
        size_t label_counter;

        if (program->statement_count == SIZE_MAX) {
            return false;
        }
        label_stride = program->statement_count + 1U;
        if (expression_id > (SIZE_MAX - label_stride) / label_stride) {
            return false;
        }
        label_counter = label_stride + expression_id * label_stride;
        if (!minic_riscv64_emit_block(file,
                                      program,
                                      function,
                                      expression->value.statement_expression.block,
                                      &label_counter)) {
            return false;
        }
''',
'''    case MINIC_EXPRESSION_STATEMENT: {
        if (!minic_riscv64_emit_expression_owned_block(
                file,
                program,
                function,
                expression_id,
                expression->value.statement_expression.block)) {
            return false;
        }
''', "statement expression block reuse")
write(p, s)

write("tests/compiler/c0/gnu_record_compound_literal.c", r'''typedef unsigned long size_t;

struct Holder {
    int tag;
    union {
        struct {
            void *ptr;
            size_t count;
        };
        size_t raw;
    };
};

static int left_effect(void)
{
    return 11;
}

static int init_effect(void)
{
    return 22;
}

/* Linux iov_iter_ubuf shape: assign a designated record compound literal. */
void assign_holder(struct Holder *out, void *ptr, size_t count)
{
    *out = (struct Holder) {
        .tag = 1,
        .ptr = ptr,
        .count = count,
    };
}

int compound_member(void)
{
    return ((struct Holder) { .tag = 7 }).tag;
}

int compound_address_and_order(void)
{
    int left = left_effect();
    struct Holder *holder = &((struct Holder) {
        .tag = init_effect(),
        .ptr = (void *)0,
        .count = 3,
    });
    return left + holder->tag + (int)holder->count;
}
''')

write("tests/compiler/c0/run-gnu-record-compound-literal.sh", r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-record-compound-literal

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_record_compound_literal.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'assign_holder:' "$work/output.s" >/dev/null
grep -F 'compound_member:' "$work/output.s" >/dev/null
grep -F 'compound_address_and_order:' "$work/output.s" >/dev/null
left_call=$(grep -n -m1 -F '  call left_effect' "$work/output.s" | cut -d: -f1)
init_call=$(grep -n -m1 -F '  call init_effect' "$work/output.s" | cut -d: -f1)
test -n "$left_call"
test -n "$init_call"
test "$left_call" -lt "$init_call"

cat >"$work/scalar.c" <<'EOF'
int scalar_compound(void)
{
    return (int) { 1 };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/scalar.c" -o "$work/scalar.i"
if "$minic" -S "$work/scalar.i" -o "$work/scalar.s" 2>"$work/scalar.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: scalar compound literal accepted' >&2
  exit 1
fi
grep -F 'compound literals currently require a block-scope record type' "$work/scalar.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_record_compound_literal record-lvalue=1 hidden-auto-local=1 initializer-block=expression-owned promoted-designators=1 record-copy=1 member-postfix=1 address-of=1 evaluation-order=preserved scalar=bounded-reject'
''')

p = "tools/dev/pr76-focused.sh"
s = read(p)
s = one(s,
'''sh tests/compiler/c0/run-gnu-statement-record-value.sh
''',
'''sh tests/compiler/c0/run-gnu-statement-record-value.sh
sh tests/compiler/c0/run-gnu-record-compound-literal.sh
''', "focused registration")
write(p, s)
