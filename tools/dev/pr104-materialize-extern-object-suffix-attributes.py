#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

def replace_exact(path, old, new, expected=1, label="replacement"):
    text = path.read_text()
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected}, found {count}")
    path.write_text(text.replace(old, new, expected))

# Shared parser-internal API: section decoding and object attribute policy must
# not differ between deferred declaration-head attributes and live suffixes.
h = root / "src/frontend/parser_internal.h"
old = '''bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,\n                                              MinicParsedAttributeList *attributes);\nbool minic_parser_apply_alignment_attribute(MinicParser *parser,\n'''
new = '''bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,\n                                              MinicParsedAttributeList *attributes);\nbool minic_parser_apply_section_attribute(MinicParser *parser,\n                                          const MinicParsedAttribute *attribute,\n                                          char *buffer,\n                                          size_t capacity,\n                                          size_t *length,\n                                          bool *has_section);\nbool minic_parser_apply_object_attribute_list(MinicParser *parser,\n                                              const MinicParsedAttributeList *attributes,\n                                              char *section_name,\n                                              size_t section_capacity,\n                                              size_t *section_name_length,\n                                              bool *has_section,\n                                              size_t *explicit_alignment);\nbool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,\n                                                   char *section_name,\n                                                   size_t section_capacity,\n                                                   size_t *section_name_length,\n                                                   bool *has_section,\n                                                   size_t *explicit_alignment);\nbool minic_parser_apply_alignment_attribute(MinicParser *parser,\n'''
replace_exact(h, old, new, label="shared object attribute declarations")

# Move generic section/object attribute semantics beside the generic attribute
# parser.  This becomes the one owner for OBJECT target policy.
pa = root / "src/frontend/parser_attribute.c"
text = pa.read_text()
anchor = '''bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n'''
insert = r'''bool minic_parser_apply_section_attribute(MinicParser *parser,
                                          const MinicParsedAttribute *attribute,
                                          char *buffer,
                                          size_t capacity,
                                          size_t *length,
                                          bool *has_section) {
    size_t cursor;
    size_t end;
    char parsed[256];
    size_t parsed_length;
    bool saw_literal;

    if (parser == NULL || attribute == NULL || buffer == NULL || length == NULL ||
        has_section == NULL || capacity == 0U || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    cursor = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    parsed_length = 0U;
    saw_literal = false;
    while (cursor < end) {
        while (cursor < end && (parser->source[cursor] == ' ' || parser->source[cursor] == '\t' ||
                                parser->source[cursor] == '\n' || parser->source[cursor] == '\r' ||
                                parser->source[cursor] == '\f' || parser->source[cursor] == '\v')) {
            cursor += 1U;
        }
        if (cursor >= end) {
            break;
        }
        if (parser->source[cursor] != '"') {
            minic_parser_error(parser,
                               "GNU section attribute requires concatenated string literals");
            return false;
        }
        saw_literal = true;
        cursor += 1U;
        while (cursor < end && parser->source[cursor] != '"') {
            if (parser->source[cursor] == '\\') {
                minic_parser_error(parser, "escaped GNU section names are not supported yet");
                return false;
            }
            if (parsed_length + 1U >= sizeof(parsed)) {
                minic_parser_error(parser, "GNU section name is too long");
                return false;
            }
            parsed[parsed_length++] = parser->source[cursor++];
        }
        if (cursor >= end || parser->source[cursor] != '"') {
            minic_parser_error(parser, "unterminated GNU section string");
            return false;
        }
        cursor += 1U;
    }
    if (!saw_literal || parsed_length == 0U || parsed_length + 1U > capacity) {
        minic_parser_error(parser, "invalid GNU section attribute argument");
        return false;
    }
    parsed[parsed_length] = '\0';
    if (*has_section) {
        if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
            minic_parser_error(parser, "conflicting GNU section attributes");
            return false;
        }
        return true;
    }
    (void)memcpy(buffer, parsed, parsed_length + 1U);
    *length = parsed_length;
    *has_section = true;
    return true;
}

typedef struct MinicObjectAttributeContext {
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    size_t *explicit_alignment;
} MinicObjectAttributeContext;

static bool object_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool consume_object_attribute(MinicParser *parser,
                                     const MinicParsedAttribute *attribute,
                                     void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;
    MinicObjectAttributeContext *context;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicObjectAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "unsupported GNU object attribute");
        return false;
    }
    if (object_attribute_class_is_parse_only(descriptor->semantic_class)) {
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
        return minic_parser_apply_section_attribute(parser,
                                                    attribute,
                                                    context->section_name,
                                                    context->section_capacity,
                                                    context->section_name_length,
                                                    context->has_section);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "object", context->explicit_alignment);
    }
    minic_parser_error(parser,
                       "unsupported GNU object attribute; symbol/layout attributes require "
                       "explicit object semantics");
    return false;
}

static bool initialize_object_attribute_context(MinicObjectAttributeContext *context,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    if (context == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    context->section_name = section_name;
    context->section_capacity = section_capacity;
    context->section_name_length = section_name_length;
    context->has_section = has_section;
    context->explicit_alignment = explicit_alignment;
    return true;
}

bool minic_parser_apply_object_attribute_list(MinicParser *parser,
                                              const MinicParsedAttributeList *attributes,
                                              char *section_name,
                                              size_t section_capacity,
                                              size_t *section_name_length,
                                              bool *has_section,
                                              size_t *explicit_alignment) {
    MinicObjectAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_object_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,
                                                   char *section_name,
                                                   size_t section_capacity,
                                                   size_t *section_name_length,
                                                   bool *has_section,
                                                   size_t *explicit_alignment) {
    MinicObjectAttributeContext context;

    if (parser == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    return minic_parser_parse_gnu_attribute_lists(parser, consume_object_attribute, &context);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"parser_attribute insertion anchor count={text.count(anchor)}")
pa.write_text(text.replace(anchor, insert + anchor, 1))

# parser_function: consume the new shared services, removing the private
# duplicate section/object policy.
pf = root / "src/frontend/parser_function.c"
text = pf.read_text()
forward = '''static bool decode_deferred_section_argument(MinicParser *parser,\n                                             const MinicParsedAttribute *attribute,\n                                             char *buffer,\n                                             size_t capacity,\n                                             size_t *length,\n                                             bool *has_section);\n\n'''
if text.count(forward) != 1:
    raise SystemExit("section forward declaration mismatch")
text = text.replace(forward, "", 1)
text = text.replace("decode_deferred_section_argument(parser,", "minic_parser_apply_section_attribute(parser,")

start = text.find("static bool decode_deferred_section_argument(")
end = text.find("static bool section_attribute_token_is(", start)
if start < 0 or end < 0:
    raise SystemExit("private section decoder boundaries missing")
private_block = text[start:end]
# Keep parse_gnu_section_attribute and function helpers; remove both the decoder
# and the private object-list consumer that sits before section token helpers.
obj_start = private_block.find("static bool apply_object_attribute_list(")
if obj_start < 0:
    raise SystemExit("private object consumer missing")
# decoder comes first, then apply_object_attribute_list; removing the whole span
# is correct because both are replaced by parser_attribute.c services.
text = text[:start] + text[end:]
text = text.replace("apply_object_attribute_list(parser,", "minic_parser_apply_object_attribute_list(parser,")
pf.write_text(text)

# Extern per-declarator suffixes now use the same OBJECT consumer as deferred
# prefix attributes.  Snapshot shared prefix alignment for each declarator.
pg = root / "src/frontend/parser_global.c"
text = pg.read_text()
old = '''        size_t declarator_section_name_length;\n        bool declarator_has_section;\n        bool is_array;\n        MinicType declarator_element_type;\n        size_t array_type_begin;\n\n        declarator_section_name_length = section_name_length;\n        declarator_has_section = has_section;\n'''
new = '''        size_t declarator_section_name_length;\n        size_t declarator_explicit_alignment;\n        bool declarator_has_section;\n        bool is_array;\n        MinicType declarator_element_type;\n        size_t array_type_begin;\n\n        declarator_section_name_length = section_name_length;\n        declarator_explicit_alignment = explicit_alignment;\n        declarator_has_section = has_section;\n'''
if text.count(old) != 1:
    raise SystemExit(f"declarator attribute state anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old_call = '''        if (!minic_parser_parse_gnu_section_attribute(parser,\n                                                      declarator_section_name,\n                                                      sizeof(declarator_section_name),\n                                                      &declarator_section_name_length,\n                                                      &declarator_has_section)) {\n            return false;\n        }\n'''
new_call = '''        if (!minic_parser_parse_gnu_object_attribute_lists(parser,\n                                                           declarator_section_name,\n                                                           sizeof(declarator_section_name),\n                                                           &declarator_section_name_length,\n                                                           &declarator_has_section,\n                                                           &declarator_explicit_alignment)) {\n            return false;\n        }\n'''
if text.count(old_call) != 1:
    raise SystemExit(f"pre-array suffix section-only call count={text.count(old_call)}")
text = text.replace(old_call, new_call, 1)

old_pair = '''        if (!minic_parser_parse_array_declarator_suffix(\n                parser, object_type, true, &object_type, &is_array) ||\n            !minic_parser_parse_gnu_section_attribute(parser,\n                                                      declarator_section_name,\n                                                      sizeof(declarator_section_name),\n                                                      &declarator_section_name_length,\n                                                      &declarator_has_section)) {\n            return false;\n        }\n'''
new_pair = '''        if (!minic_parser_parse_array_declarator_suffix(\n                parser, object_type, true, &object_type, &is_array) ||\n            !minic_parser_parse_gnu_object_attribute_lists(parser,\n                                                           declarator_section_name,\n                                                           sizeof(declarator_section_name),\n                                                           &declarator_section_name_length,\n                                                           &declarator_has_section,\n                                                           &declarator_explicit_alignment)) {\n            return false;\n        }\n'''
if text.count(old_pair) != 1:
    raise SystemExit(f"post-array suffix section-only call count={text.count(old_pair)}")
text = text.replace(old_pair, new_pair, 1)
# Only within this function, the two storage/merge sites should consume the
# per-declarator value, not the shared declaration-head value.
func_start = text.find("bool minic_parser_parse_extern_global_after_head(")
func_end = text.find("bool minic_parser_parse_extern_global(MinicParser *parser)", func_start)
if func_start < 0 or func_end < 0:
    raise SystemExit("extern after-head boundaries missing")
chunk = text[func_start:func_end]
if chunk.count("explicit_alignment,") != 2:
    raise SystemExit(f"expected 2 alignment consumers, found {chunk.count('explicit_alignment,')}")
chunk = chunk.replace("explicit_alignment,", "declarator_explicit_alignment,")
text = text[:func_start] + chunk + text[func_end:]

# Legacy direct extern entry also gains generic object attributes after the
# type, preserving the same semantics if this path is used independently.
old_legacy = '''    size_t section_name_length;\n    bool has_section;\n\n    section_name_length = 0U;\n    has_section = false;\n'''
new_legacy = '''    size_t section_name_length;\n    size_t explicit_alignment;\n    bool has_section;\n\n    section_name_length = 0U;\n    explicit_alignment = 0U;\n    has_section = false;\n'''
if text.count(old_legacy) != 1:
    raise SystemExit(f"legacy extern state anchor count={text.count(old_legacy)}")
text = text.replace(old_legacy, new_legacy, 1)
old_legacy_call = '''        !minic_parser_parse_gnu_section_attribute(\n            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||\n'''
new_legacy_call = '''        !minic_parser_parse_gnu_object_attribute_lists(parser,\n                                                       section_name,\n                                                       sizeof(section_name),\n                                                       &section_name_length,\n                                                       &has_section,\n                                                       &explicit_alignment) ||\n'''
if text.count(old_legacy_call) != 1:
    raise SystemExit(f"legacy extern section call count={text.count(old_legacy_call)}")
text = text.replace(old_legacy_call, new_legacy_call, 1)
old_tail = '''                                                       has_section,\n                                                       0U,\n                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,\n'''
new_tail = '''                                                       has_section,\n                                                       explicit_alignment,\n                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,\n'''
if text.count(old_tail) != 1:
    raise SystemExit("legacy alignment tail mismatch")
text = text.replace(old_tail, new_tail, 1)
pg.write_text(text)

# Extend the existing object-alignment regression rather than create a parallel
# suite.  Freeze the exact Linux placement plus per-declarator isolation.
test = root / "tests/compiler/c0/gnu_object_alignment_attribute.c"
text = test.read_text()
old = '''typedef unsigned long long u64;\n\nextern u64 __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies_64;\n'''
new = '''typedef unsigned long long u64;\n\ntypedef struct {\n    unsigned int __softirq_pending;\n} irq_cpustat_t;\n\nextern __attribute__((section(".data..percpu" "..shared_aligned")))\n__typeof__(irq_cpustat_t) irq_stat __attribute__((__aligned__((1 << 6))));\n\nextern __attribute__((section(".probe.suffix.aligned"))) __typeof__(u64) suffix_aligned\n    __attribute__((__aligned__((1 << 6))));\nextern u64 isolated_aligned __attribute__((__aligned__((1 << 6)))), isolated_natural;\n\nextern u64 __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies_64;\n'''
if text.count(old) != 1:
    raise SystemExit("object alignment test header mismatch")
text = text.replace(old, new, 1)
old_defs = '''u64 ordinary = 1;\nu64 jiffies_64 = 0;\nunsigned long volatile jiffies = 0;\n'''
new_defs = '''u64 ordinary = 1;\nu64 suffix_aligned = 0;\nu64 isolated_aligned = 0;\nu64 isolated_natural = 0;\nu64 jiffies_64 = 0;\nunsigned long volatile jiffies = 0;\n'''
if text.count(old_defs) != 1:
    raise SystemExit("object alignment definition anchor mismatch")
test.write_text(text.replace(old_defs, new_defs, 1))

runner = root / "tests/compiler/c0/run-gnu-object-alignment-attribute.sh"
text = runner.read_text()
old_awk = '''    /^jiffies_64:$/ { if (align == 6) j64 = 1; next }\n    /^jiffies:$/ { if (align == 6) j = 1; next }\n    /^ordinary:$/ { if (align == 3) ordinary = 1; next }\n    END { exit !(j64 && j && ordinary) }\n'''
new_awk = '''    /^jiffies_64:$/ { if (align == 6) j64 = 1; next }\n    /^jiffies:$/ { if (align == 6) j = 1; next }\n    /^suffix_aligned:$/ { if (align == 6) suffix = 1; next }\n    /^isolated_aligned:$/ { if (align == 6) isolated_a = 1; next }\n    /^isolated_natural:$/ { if (align == 3) isolated_b = 1; next }\n    /^ordinary:$/ { if (align == 3) ordinary = 1; next }\n    END { exit !(j64 && j && suffix && isolated_a && isolated_b && ordinary) }\n'''
if text.count(old_awk) != 1:
    raise SystemExit("object alignment awk anchor mismatch")
text = text.replace(old_awk, new_awk, 1)
old_section = '''test "$(grep -c '^\\.section \\.data\\.\\.cacheline_aligned$' "$work/output.s")" -eq 2\n'''
new_section = old_section + '''test "$(grep -c '^\\.section \\.probe\\.suffix\\.aligned$' "$work/output.s")" -eq 1\n'''
if text.count(old_section) != 1:
    raise SystemExit("section check anchor mismatch")
text = text.replace(old_section, new_section, 1)
old_invalid = '''extern int __attribute__((__aligned__(24))) invalid_alignment;\n'''
new_invalid = '''extern int invalid_alignment __attribute__((__aligned__(24)));\n'''
if text.count(old_invalid) != 1:
    raise SystemExit("invalid alignment shape mismatch")
text = text.replace(old_invalid, new_invalid, 1)
old_pass = '''printf '%s\\n' "PASS compiler/c0/gnu_object_alignment_attribute ownership=object alignment=64 section=preserved type-contamination=none invalid=reject"\n'''
new_pass = '''printf '%s\\n' "PASS compiler/c0/gnu_object_alignment_attribute ownership=object alignment=64 prefix+suffix=shared-consumer typeof-record-linux-shape=1 per-declarator=isolated section=preserved type-contamination=none invalid=reject"\n'''
if text.count(old_pass) != 1:
    raise SystemExit("pass text mismatch")
runner.write_text(text.replace(old_pass, new_pass, 1))
