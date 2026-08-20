#!/usr/bin/env python3
"""Materialize GNU attributes interleaved between function return-pointer levels."""
from pathlib import Path

parser_path = Path("src/frontend/parser_function.c")
text = parser_path.read_text()

old = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &return_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
'''
new = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
    return_type = base_type;
    for (;;) {
        if (!minic_parser_parse_pointer_declarator(parser, return_type, &return_type) ||
            !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
            return false;
        }
        /* GNU permits declarator attributes between pointer levels, for example
           `char * __attribute__((unused)) *fn(void)`.  Keep those attributes in
           the existing deferred entity-routing list, then resume the same pointer
           declarator rather than mistaking the following `*` for a missing name. */
        if (parser->current.kind != MINIC_TOKEN_STAR) {
            break;
        }
    }
'''
if new not in text:
    if old not in text:
        raise SystemExit("function return-pointer attribute anchor not found")
    text = text.replace(old, new, 1)
parser_path.write_text(text)

test_path = Path("tests/compiler/c0/deferred_declarator_attributes.c")
test = test_path.read_text()
anchor = '''void *map_after_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

'''
addition = '''void *map_after_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

char * __attribute__((__unused__)) *interleaved_return_pointer_attribute(void) {
    return (char **)0;
}

'''
if addition not in test:
    if anchor not in test:
        raise SystemExit("interleaved return-pointer attribute test anchor not found")
    test = test.replace(anchor, addition, 1)
test_path.write_text(test)

run_path = Path("tests/compiler/c0/run-deferred-declarator-attributes.sh")
run = run_path.read_text()
anchor = '''grep -F 'map_after_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_before_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
'''
addition = '''grep -F 'map_after_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'interleaved_return_pointer_attribute:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_before_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
'''
if addition not in run:
    if anchor not in run:
        raise SystemExit("interleaved return-pointer attribute run anchor not found")
    run = run.replace(anchor, addition, 1)
old_pass = "PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg used=parse-only+function-object+zero-arg fp-object-interleaved=collected+object-routed typedef-interleaved=fail-closed"
new_pass = "PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic interleaved-return-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg used=parse-only+function-object+zero-arg fp-object-interleaved=collected+object-routed typedef-interleaved=fail-closed"
if new_pass not in run:
    if old_pass not in run:
        raise SystemExit("interleaved return-pointer attribute pass anchor not found")
    run = run.replace(old_pass, new_pass, 1)
run_path.write_text(run)
