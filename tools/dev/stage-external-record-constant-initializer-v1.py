#!/usr/bin/env python3
from pathlib import Path

# Promote the existing static-storage initializer value parser to a parser-internal owner.
path = Path('src/frontend/parser_global.c')
text = path.read_text()
text = text.replace('parse_static_constant_value', 'minic_parser_parse_static_storage_initializer_value')
old = '''static bool
minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                    MinicGlobalObjectId object_id,
                                                    MinicType type);
'''
new = '''bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''static bool
minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                    MinicGlobalObjectId object_id,
                                                    MinicType type) {
'''
new = '''bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
path.write_text(text)

# Declare the narrow internal seam next to other cross-parser helpers.
path = Path('src/frontend/parser_internal.h')
text = path.read_text()
anchor = '''bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,
                                                 MinicType target_type,
                                                 uint64_t *bits);
'''
addition = '''bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,
                                                 MinicType target_type,
                                                 uint64_t *bits);
bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);
'''
assert text.count(anchor) == 1
path.write_text(text.replace(anchor, addition, 1))

# External-linkage object definitions have the same static storage-duration constant initializer
# semantics. Reuse the owner for records instead of duplicating aggregate parsing.
path = Path('src/frontend/parser_function.c')
text = path.read_text()
old = '''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }
'''
new = '''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, object_type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }

    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
'''
new = '''    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, object_type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }
    if (minic_type_is_record(object_type)) {
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external record initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }

    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))

# Permanent focused Linux-shaped external record compound literal.
Path('tests/compiler/c0/external_record_compound_literal.c').write_text(r'''struct counter {
    int value;
};

union key_payload {
    unsigned long type;
    void *entries;
};

struct static_key {
    struct counter enabled;
    union key_payload payload;
};

struct static_key_false {
    struct static_key key;
};

struct static_key_false sched_numa_balancing =
    (struct static_key_false){ .key = { .enabled = { 0 }, { .type = 0UL } }, };

int main(void) {
    return sched_numa_balancing.key.enabled.value;
}
''')

Path('tests/compiler/c0/run-external-record-compound-literal.sh').write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-record-compound-literal

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/external_record_compound_literal.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.globl sched_numa_balancing' "$work/output.s" >/dev/null
grep -F 'sched_numa_balancing:' "$work/output.s" >/dev/null
grep -F 'main:' "$work/output.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/external-record-compound-literal linkage=external storage=static-duration initializer=shared-constant-owner compound-literal=record designated+nested-union=1'
''')

path = Path('tests/compiler/c0/run-foundation-focused.sh')
text = path.read_text()
anchor = '''    run-static-nested-record-initializers.sh \\
    run-static-local-scalars.sh \\
'''
replacement = '''    run-static-nested-record-initializers.sh \\
    run-external-record-compound-literal.sh \\
    run-static-local-scalars.sh \\
'''
assert text.count(anchor) == 1
path.write_text(text.replace(anchor, replacement, 1))
