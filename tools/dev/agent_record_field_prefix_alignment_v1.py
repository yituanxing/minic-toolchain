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


p = "src/frontend/parser_record.c"
s = read(p)
s = one(
    s,
    '''static bool
parse_record_field_declarator(MinicParser *parser, MinicRecordId record_id, MinicType base_type) {
''',
    '''static bool parse_record_field_declarator(MinicParser *parser,
                                          MinicRecordId record_id,
                                          MinicType base_type,
                                          size_t declaration_alignment) {
''',
    "field declarator signature",
)
s = one(
    s,
    '''    element_count = 1U;
    explicit_alignment = 0U;
''',
    '''    element_count = 1U;
    explicit_alignment = declaration_alignment;
''',
    "field declarator inherited alignment",
)
s = one(
    s,
    '''    context.explicit_alignment = 0U;
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {
''',
    '''    context.explicit_alignment = *explicit_alignment;
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_record_field_attribute, &context)) {
''',
    "field attribute merge state",
)
s = one(
    s,
    '''static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicType base_type;
    const MinicRecord *record;
''',
    '''static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicType base_type;
    const MinicRecord *record;
    size_t declaration_alignment;
''',
    "field declaration state",
)
s = one(
    s,
    '''    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {
''',
    '''    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
    declaration_alignment = 0U;
    if (!parse_record_field_attributes(parser, &declaration_alignment)) {
        return false;
    }
    if (minic_type_is_record(base_type) && parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (declaration_alignment != 0U) {
            minic_parser_error(parser,
                               "GNU alignment on anonymous record members is unsupported");
            return false;
        }
''',
    "field prefix attributes",
)
s = one(
    s,
    '''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        size_t bit_width;

        if (!parse_record_bit_field_width(parser, base_type, true, &bit_width) ||
''',
    '''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        size_t bit_width;

        if (declaration_alignment != 0U) {
            minic_parser_error(parser,
                               "GNU alignment on unnamed bit-fields is unsupported");
            return false;
        }
        if (!parse_record_bit_field_width(parser, base_type, true, &bit_width) ||
''',
    "unnamed bitfield prefix alignment boundary",
)
s = one(
    s,
    '''        if (!parse_record_field_declarator(parser, record_id, base_type)) {
''',
    '''        if (!parse_record_field_declarator(
                parser, record_id, base_type, declaration_alignment)) {
''',
    "field declarator consumer",
)
write(p, s)

p = "tests/compiler/c0/gnu_aligned_record_field.c"
s = read(p)
s += r'''

typedef unsigned long long __u64;

/* Linux uapi sched.h shape: field alignment between type specifier and name. */
struct PrefixAlignedField {
    char prefix;
    __u64 __attribute__((aligned(8))) flags;
    char tail;
};

int prefix_aligned_flags_offset(void) {
    return __builtin_offsetof(struct PrefixAlignedField, flags);
}

int prefix_aligned_tail_offset(void) {
    return __builtin_offsetof(struct PrefixAlignedField, tail);
}

/* Prefix and suffix placements must share one consumer and merge by max. */
struct MixedAlignedField {
    char prefix;
    unsigned long long __attribute__((aligned(8))) value
        __attribute__((aligned(16)));
    char tail;
};

int mixed_aligned_value_offset(void) {
    return __builtin_offsetof(struct MixedAlignedField, value);
}

int mixed_aligned_tail_offset(void) {
    return __builtin_offsetof(struct MixedAlignedField, tail);
}

int mixed_aligned_record_size(void) {
    return sizeof(struct MixedAlignedField);
}
'''
write(p, s)

p = "tests/compiler/c0/run-gnu-aligned-record-fields.sh"
s = read(p)
s = one(
    s,
    '''grep -F 'aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 16' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 32' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 48' "$work/gnu_aligned_record_field.s" >/dev/null

printf '%s\\n' 'PASS compiler/c0/gnu_aligned_record_field minimum-align=16 typed-ast-consteval=1 values-offset=16 tail-offset=32 record-size=48 offsetof=consistent'
''',
    '''grep -F 'aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'prefix_aligned_flags_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'prefix_aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_value_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
# Existing suffix alignment contract: 16,32,48.
grep -F '  li a0, 48' "$work/gnu_aligned_record_field.s" >/dev/null
# Linux prefix shape naturally places u64 at 8 and its tail at 16.
grep -F '  li a0, 8' "$work/gnu_aligned_record_field.s" >/dev/null
# Prefix aligned(8) + suffix aligned(16) must merge to 16, with tail at 24 and size 32.
grep -F '  li a0, 24' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 32' "$work/gnu_aligned_record_field.s" >/dev/null

printf '%s\\n' 'PASS compiler/c0/gnu_aligned_record_field minimum-align=16 typed-ast-consteval=1 placement=suffix+pre-declarator linux-prefix-shape=1 prefix-suffix-merge=max values-offset=16 tail-offset=32 record-size=48 offsetof=consistent'
''',
    "aligned field runner",
)
write(p, s)
