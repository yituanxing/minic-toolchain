#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start_marker = "static bool add_general_prefix_for_update("
end_marker = "static bool token_starts_local_declaration(const MinicParser *parser);"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("for-update: cannot find legacy helper range")
new_update = r'''static bool parse_for_update(MinicParser *parser, MinicStatementId *statement_id) {
    const MinicExpression *expression;
    MinicStatement statement;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, statement.expression);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid for update expression");
        return false;
    }
    statement.span = expression->span;
    return minic_c0_program_add_statement(parser->program, &statement, statement_id);
}

'''
text = text[:start] + new_update + text[end:]

text = replace_once(
    text,
    "    MinicStatementId updates[8];\n"
    "    MinicStatementId continue_label;\n"
    "    MinicStatementId previous_continue_target;\n"
    "    size_t update_count;\n"
    "    size_t update_index;\n",
    "    MinicStatementId update_statement;\n"
    "    MinicStatementId continue_label;\n"
    "    MinicStatementId previous_continue_target;\n"
    "    bool has_update;\n",
    "for-declarations",
)

update_begin = text.find("    update_count = 0U;\n")
update_end = text.find(
    "    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, \"expected ')'\") ||",
    update_begin,
)
if update_begin < 0 or update_end < 0:
    raise SystemExit("for-update: cannot find update-list range")
new_update_parse = r'''    has_update = false;
    update_statement = MINIC_STATEMENT_INVALID;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (!parse_for_update(parser, &update_statement)) {
            return false;
        }
        has_update = true;
    }

'''
text = text[:update_begin] + new_update_parse + text[update_end:]

append_begin = text.find(
    "    for (update_index = 0U; update_index < update_count; ++update_index) {\n"
)
append_end = text.find("    statement.span.begin = for_span.begin;\n", append_begin)
if append_begin < 0 or append_end < 0:
    raise SystemExit("for-update: cannot find update-append range")
new_append = r'''    if (has_update &&
        !minic_c0_block_add_statement(parser->program, statement.then_block, update_statement)) {
        minic_parser_error(parser, "cannot append for-loop update");
        return false;
    }
'''
text = text[:append_begin] + new_append + text[append_end:]
path.write_text(text)

Path("tests/compiler/c0/for_expression_initializer.c").write_text(
r'''int for_compound_initializer(void) {
    int i = 7;
    int total = 0;

    for (i -= 2; i > 0; i--) {
        total += i;
    }
    return total;
}

int for_comma_initializer(void) {
    int i = 9;
    int scale = 9;

    for (i = 0, scale = 1; i < 3; i++) {
        scale *= 2;
    }
    return i + scale;
}

static unsigned int next_bit_like(unsigned int value) {
    return value + 2U;
}

int for_parenthesized_post_update(void) {
    unsigned int i;
    int total = 0;

    for ((i) = 0; (i) = next_bit_like(i), (i) < 8U; (i)++) {
        total += (int)i;
    }
    return total;
}

int for_comma_update(void) {
    int i = 0;
    int scale = 0;

    for (; i < 3; (i)++, scale += 2) {
    }
    return i + scale;
}
''')

Path("tests/compiler/c0/run-for-expression-initializers.sh").write_text(
r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-for-expression-initializer

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/for_expression_initializer.c" \
    -o "$work/for_expression_initializer.i"
"$minic" -S "$work/for_expression_initializer.i" \
    -o "$work/for_expression_initializer.s"

grep -F 'for_compound_initializer:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_comma_initializer:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_parenthesized_post_update:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_comma_update:' "$work/for_expression_initializer.s" >/dev/null
grep -E '^[[:space:]]+subw[[:space:]]+a0,' "$work/for_expression_initializer.s" >/dev/null
grep -F '  beqz a0, .Lwhile_end_' "$work/for_expression_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/for_expression_initializer compound=-= expression-init=general comma-init=2 update=full-expression,parenthesized-post++,comma-expression'
''')
