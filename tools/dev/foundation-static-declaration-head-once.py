#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_range(text: str, start: str, end: str, new: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start anchor not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:begin] + new + text[finish:]


root = Path(__file__).resolve().parents[2]

# Contract: consumers may receive an already-parsed static object head.
path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    "bool minic_parser_parse_static_global(MinicParser *parser);\n",
    "bool minic_parser_parse_static_global(MinicParser *parser);\n"
    "bool minic_parser_parse_static_global_after_head(MinicParser *parser,\n"
    "                                                 MinicType object_type,\n"
    "                                                 MinicSourceSpan name_span);\n",
    "static-after-head-prototype",
)
path.write_text(text)

# Static global parser: keep the existing initializer/object tail, split only the consumed head.
path = root / "src/frontend/parser_global.c"
text = path.read_text()
text = replace_once(
    text,
    "bool minic_parser_parse_static_global(MinicParser *parser) {\n"
    "    MinicSourceSpan name_span;\n"
    "    MinicType element_type;\n",
    "bool minic_parser_parse_static_global_after_head(MinicParser *parser,\n"
    "                                                 MinicType element_type,\n"
    "                                                 MinicSourceSpan name_span) {\n",
    "static-after-head-signature",
)
old_head = r'''    bound_count = 0U;
    expected_count = 1U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &element_type)) {
        return false;
    }
    if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
        !minic_type_is_record(element_type)) {
        minic_parser_error(parser, "unsupported static global object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }
'''
new_head = r'''    bound_count = 0U;
    expected_count = 1U;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_record(element_type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "unsupported static global object type");
        }
        return false;
    }
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
'''
text = replace_once(text, old_head, new_head, "static-after-head-body")
wrapper = r'''

bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;

    if (parser == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }
    name_span = parser->current.span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_parse_static_global_after_head(parser, object_type, name_span);
}
'''
if "bool minic_parser_parse_static_global_after_head" not in text:
    raise SystemExit("static-after-head implementation missing after rewrite")
text = text.rstrip() + wrapper + "\n"
path.write_text(text)

# Top-level parser: classify static function/object from the one real parse, never a semantic probe.
path = root / "src/frontend/parser_function.c"
text = path.read_text()
text = text.replace("validate_external_object_attribute_list", "validate_object_attribute_list")
text = text.replace("unsupported GNU external-object prefix attribute; symbol/layout ",
                    "unsupported GNU object prefix attribute; symbol/layout ")
text = replace_once(
    text,
    "    bool is_function_pointer_object;\n    bool is_inline;\n",
    "    bool is_function_pointer_object;\n    bool is_inline;\n    bool is_static_declaration;\n",
    "static-head-state",
)
text = replace_once(
    text,
    "    is_function_pointer_object = false;\n    is_inline = false;\n",
    "    is_function_pointer_object = false;\n    is_inline = false;\n    is_static_declaration = is_internal;\n",
    "static-head-init",
)
text = replace_once(
    text,
    '''    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        is_internal = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
''',
    '''    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        is_internal = true;
        is_static_declaration = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
''',
    "static-storage-state",
)
object_dispatch_anchor = '''    if (!is_internal &&
        (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {
'''
static_dispatch = r'''    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!validate_object_attribute_list(parser, &deferred_attributes)) {
            return false;
        }
        if (has_section || has_visibility) {
            minic_parser_error(parser,
                               "static object symbol attributes require explicit object semantics");
            return false;
        }
        return minic_parser_parse_static_global_after_head(parser, return_type, name_span);
    }
'''
text = replace_once(text, object_dispatch_anchor, static_dispatch + object_dispatch_anchor,
                    "static-object-after-head-dispatch")
# Remove the old semantic probe entirely.
text = replace_range(
    text,
    "static bool static_declaration_is_function(MinicParser *parser, bool *is_function) {",
    "static bool enum_keyword_starts_definition",
    "",
    "remove-static-semantic-probe",
)
old_top = r'''        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            bool is_function;

            if (!static_declaration_is_function(&parser, &is_function)) {
                success = false;
            } else if (is_function) {
                success = parse_function(&parser, true);
            } else {
                success = minic_parser_parse_static_global(&parser);
            }
'''
new_top = r'''        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            success = parse_function(&parser, true);
'''
text = replace_once(text, old_top, new_top, "static-top-level-dispatch")
if "static_declaration_is_function" in text:
    raise SystemExit("static semantic probe still present")
path.write_text(text)

# Focused regression: exact Linux ordering plus a static-inline function on the same dispatch path.
fixture = root / "tests/compiler/c0/static_prefix_object_attributes.c"
fixture.write_text(r'''static __attribute__((__unused__)) const int class_irq_is_conditional = 0;

static inline __attribute__((__always_inline__)) int class_irq_add(int value) {
    return value + 1;
}

int main(void) {
    return class_irq_is_conditional == 0 && class_irq_add(6) == 7 ? 0 : 1;
}
''')
runner = root / "tests/compiler/c0/run-static-prefix-object-attributes.sh"
runner.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-prefix-object-attributes
assembly="$work/static_prefix_object_attributes.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_prefix_object_attributes.c" \
    -o "$work/static_prefix_object_attributes.i"
"$minic" -S "$work/static_prefix_object_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F 'class_irq_is_conditional:' "$assembly" >/dev/null
grep -F 'class_irq_add:' "$assembly" >/dev/null
if grep -q 'static_declaration_is_function' src/frontend/parser_function.c; then
    printf '%s\n' 'static semantic declaration probe still present' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_prefix_object_attributes prefix=unused object=static-const function=static-inline single-pass-head=1 semantic-probe=none'
''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    "sh tests/compiler/c0/run-function-linkage-inheritance.sh\n",
    "sh tests/compiler/c0/run-function-linkage-inheritance.sh\n"
    "sh tests/compiler/c0/run-static-prefix-object-attributes.sh\n",
    "static-head-focused-gate",
)
path.write_text(text)
