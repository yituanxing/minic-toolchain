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


p = "src/frontend/parser_function.c"
s = read(p)
s = one(
    s,
    '''typedef struct MinicFunctionAttributeContext {
    bool allow_gnu_inline;
    bool is_internal;
    bool is_inline;
    const char *unsupported_message;
} MinicFunctionAttributeContext;
''',
    '''static bool decode_deferred_section_argument(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *length,
                                             bool *has_section);

typedef struct MinicFunctionAttributeContext {
    bool allow_gnu_inline;
    bool is_internal;
    bool is_inline;
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    const char *unsupported_message;
} MinicFunctionAttributeContext;
''',
    "function attribute context",
)
s = one(
    s,
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {
''',
    '''    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
        if (context->section_name == NULL || context->section_name_length == NULL ||
            context->has_section == NULL || context->section_capacity == 0U ||
            !decode_deferred_section_argument(parser,
                                              attribute,
                                              context->section_name,
                                              context->section_capacity,
                                              context->section_name_length,
                                              context->has_section)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "%s", context->unsupported_message);
            }
            return false;
        }
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {
''',
    "function section consumer",
)
s = one(
    s,
    '''    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
''',
    '''    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.section_name = NULL;
    context.section_capacity = 0U;
    context.section_name_length = NULL;
    context.has_section = NULL;
    context.unsupported_message = unsupported_message;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
''',
    "parsed function context",
)
s = one(
    s,
    '''static bool apply_function_attribute_list(MinicParser *parser,
                                          const MinicParsedAttributeList *attributes,
                                          bool allow_gnu_inline,
                                          bool is_internal,
                                          bool is_inline,
                                          const char *unsupported_message) {
''',
    '''static bool apply_function_attribute_list(MinicParser *parser,
                                          const MinicParsedAttributeList *attributes,
                                          bool allow_gnu_inline,
                                          bool is_internal,
                                          bool is_inline,
                                          char *section_name,
                                          size_t section_capacity,
                                          size_t *section_name_length,
                                          bool *has_section,
                                          const char *unsupported_message) {
''',
    "apply function signature",
)
s = one(
    s,
    '''    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    for (index = 0U; index < attributes->count; ++index) {
''',
    '''    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.section_name = section_name;
    context.section_capacity = section_capacity;
    context.section_name_length = section_name_length;
    context.has_section = has_section;
    context.unsupported_message = unsupported_message;
    for (index = 0U; index < attributes->count; ++index) {
''',
    "applied function context",
)
s = one(
    s,
    '''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly")) {
''',
    '''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            section_name,
            sizeof(section_name),
            &section_name_length,
            &has_section,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly")) {
''',
    "apply prefix function attributes",
)
write(p, s)

p = "tests/compiler/c0/gnu_section_symbol_attribute.c"
s = read(p)
s = one(
    s,
    '''void placed_function(void) {
}

int main(void) {
''',
    '''void placed_function(void) {
}

/* Linux init/main.i shape: symbol section before the return type, optimization
 * metadata beside it, and function metadata after the declarator. */
__attribute__((__section__(".probe.prefix.text"))) __attribute__((__cold__))
void *prefix_placed_function(int nid, unsigned long size, unsigned long mask)
    __attribute__((__alloc_size__(2))) __attribute__((__malloc__));

void *prefix_placed_function(int nid, unsigned long size, unsigned long mask) {
    if (nid || size || mask) {
        return (void *)0;
    }
    return (void *)0;
}

int main(void) {
''',
    "prefix section fixture",
)
write(p, s)

p = "tests/compiler/c0/run-gnu-section-symbol-attribute.sh"
s = read(p)
s = one(
    s,
    '''grep -F '.section .probe.text' "$assembly" >/dev/null
grep -F 'placed_function:' "$assembly" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_section_symbol_attribute extern-object=prefix+suffix deferred-prefix=section concatenated-string=1 typeof-record-declaration=1 per-declarator=isolated array-suffix=1 function-declaration=preserved definition-inherits=1 rv64-section-emission=1'
''',
    '''grep -F '.section .probe.text' "$assembly" >/dev/null
grep -F 'placed_function:' "$assembly" >/dev/null
grep -F '.section .probe.prefix.text' "$assembly" >/dev/null
grep -F 'prefix_placed_function:' "$assembly" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_section_symbol_attribute extern-object=prefix+suffix deferred-prefix=section function-prefix-section=1 function-mixed-attributes=section+cold+alloc-size+malloc concatenated-string=1 typeof-record-declaration=1 per-declarator=isolated array-suffix=1 function-declaration=preserved definition-inherits=1 rv64-section-emission=1'
''',
    "section runner",
)
write(p, s)
