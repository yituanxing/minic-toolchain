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


p = "src/frontend/parser_statement.c"
s = read(p)
s = one(
    s,
    '''        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            if (!saw_value) {
                minic_parser_error(parser, "empty aggregate initializer is unsupported");
                return false;
            }
            initializer_span->begin = begin;
''',
    '''        if (parser->current.kind == MINIC_TOKEN_RBRACE) {
            initializer_span->begin = begin;
''',
    "allow empty aggregate initializer",
)
# saw_value is no longer needed once immediate '}' is a valid all-zero aggregate.
s = one(s, '''    bool saw_value;\n\n''', '', "remove saw-value declaration")
s = one(s, '''    saw_value = false;\n''', '', "remove saw-value initialization")
s = one(s, '''        saw_value = true;\n''', '', "remove saw-value update")
write(p, s)

p = "tests/compiler/c0/gnu_record_compound_literal.c"
s = read(p)
s = one(
    s,
    '''int compound_member(void)
{
    return ((struct Holder) { .tag = 7 }).tag;
}

int compound_address_and_order(void)
''',
    '''int compound_member(void)
{
    return ((struct Holder) { .tag = 7 }).tag;
}

/* Linux path_put_init shape: an empty compound literal means all-zero. */
void clear_holder(struct Holder *out)
{
    *out = (struct Holder) { };
}

int local_empty_initializer(void)
{
    struct Holder holder = { };
    return holder.tag + (int)holder.count;
}

int compound_address_and_order(void)
''',
    "empty initializer fixture",
)
write(p, s)

p = "tests/compiler/c0/run-gnu-record-compound-literal.sh"
s = read(p)
s = one(
    s,
    '''grep -F 'compound_member:' "$work/output.s" >/dev/null
grep -F 'compound_address_and_order:' "$work/output.s" >/dev/null
''',
    '''grep -F 'compound_member:' "$work/output.s" >/dev/null
grep -F 'clear_holder:' "$work/output.s" >/dev/null
grep -F 'local_empty_initializer:' "$work/output.s" >/dev/null
grep -F 'compound_address_and_order:' "$work/output.s" >/dev/null
''',
    "empty initializer runner symbols",
)
s = one(
    s,
    "record-lvalue=1 hidden-auto-local=1 initializer-block=expression-owned promoted-designators=1 record-copy=1 member-postfix=1 address-of=1 evaluation-order=preserved scalar=bounded-reject",
    "record-lvalue=1 hidden-auto-local=1 initializer-block=expression-owned empty-aggregate=compound+local promoted-designators=1 record-copy=1 member-postfix=1 address-of=1 evaluation-order=preserved scalar=bounded-reject",
    "empty initializer runner summary",
)
write(p, s)
