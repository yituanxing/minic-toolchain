#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Remove the legacy prefix-only packed parser. Prefix and suffix type attributes
# are parsed through one AttributeRegistry-backed consumer below.
replace_once(
    "src/frontend/parser_record.c",
    '''static bool parse_packed_record_attribute(MinicParser *parser, bool *is_packed) {
    if (parser == NULL || is_packed == NULL) {
        return false;
    }
    *is_packed = false;
    if (!token_text_equals(parser, parser->current, "__attribute__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' in __attribute__")) {
        return false;
    }
    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_PACKED, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "only packed record attribute is supported here");
        return false;
    }
    *is_packed = true;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after packed attribute") &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after __attribute__");
}

''',
    '',
)

old_suffix = '''typedef struct MinicRecordSuffixAttributeContext {
    MinicRecordId record_id;
    size_t explicit_alignment;
} MinicRecordSuffixAttributeContext;

static bool consume_record_suffix_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            void *opaque_context) {
    MinicRecordSuffixAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;
    const MinicRecord *record;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordSuffixAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    record = minic_c0_program_record(parser->program, context->record_id);
    if (descriptor == NULL || record == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU record suffix attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_DESIGNATED_INIT) {
        if (record->is_union) {
            minic_parser_error(parser, "GNU designated_init applies only to struct types");
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record suffix attribute");
    return false;
}

static bool parse_record_suffix_attributes(MinicParser *parser,
                                           MinicRecordId record_id,
                                           size_t *explicit_alignment) {
    MinicRecordSuffixAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL) {
        return false;
    }
    context.record_id = record_id;
    context.explicit_alignment = 0U;
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_record_suffix_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    return true;
}
'''
new_suffix = '''typedef struct MinicRecordTypeAttributeContext {
    size_t explicit_alignment;
    bool is_packed;
    bool is_union;
} MinicRecordTypeAttributeContext;

static bool consume_record_type_attribute(MinicParser *parser,
                                          const MinicParsedAttribute *attribute,
                                          void *opaque_context) {
    MinicRecordTypeAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordTypeAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU record type attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_PACKED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_LAYOUT) {
        context->is_packed = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record", &context->explicit_alignment);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_DESIGNATED_INIT) {
        if (context->is_union) {
            minic_parser_error(parser, "GNU designated_init applies only to struct types");
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "unsupported GNU record type attribute");
    return false;
}

static bool parse_record_type_attributes(MinicParser *parser,
                                         bool is_union,
                                         size_t *explicit_alignment,
                                         bool *is_packed) {
    MinicRecordTypeAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL || is_packed == NULL) {
        return false;
    }
    context.explicit_alignment = *explicit_alignment;
    context.is_packed = *is_packed;
    context.is_union = is_union;
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_record_type_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    *is_packed = context.is_packed;
    return true;
}
'''
replace_once("src/frontend/parser_record.c", old_suffix, new_suffix)

replace_once(
    "src/frontend/parser_record.c",
    '''    MinicRecordId record_id;
    MinicTokenKind record_keyword;
    bool is_packed;
    bool is_union;
''',
    '''    MinicRecordId record_id;
    MinicTokenKind record_keyword;
    size_t explicit_alignment;
    bool is_packed;
    bool is_union;
''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    if (!minic_parser_advance(parser) || !parse_packed_record_attribute(parser, &is_packed)) {
        return false;
    }
''',
    '''    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    explicit_alignment = 0U;
    is_packed = false;
    if (!minic_parser_advance(parser) ||
        !parse_record_type_attributes(parser, is_union, &explicit_alignment, &is_packed)) {
        return false;
    }
''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''            parser->program->records[record_id].is_union = is_union;
            parser->program->records[record_id].is_packed = is_packed;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union ||
                (is_packed && record->is_packed != is_packed)) {
                minic_parser_error(parser, "duplicate record definition");
                return false;
            }
        }
''',
    '''            parser->program->records[record_id].is_union = is_union;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union) {
                minic_parser_error(parser, "duplicate record definition");
                return false;
            }
        }
        parser->program->records[record_id].is_packed =
            parser->program->records[record_id].is_packed || is_packed;
        if (explicit_alignment > parser->program->records[record_id].explicit_alignment) {
            parser->program->records[record_id].explicit_alignment = explicit_alignment;
        }
''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''        parser->program->records[record_id].is_union = is_union;
        parser->program->records[record_id].is_packed = is_packed;
    } else {
''',
    '''        parser->program->records[record_id].is_union = is_union;
        parser->program->records[record_id].is_packed = is_packed;
        parser->program->records[record_id].explicit_alignment = explicit_alignment;
    } else {
''',
)

replace_once(
    "src/frontend/parser_record.c",
    '''    {
        size_t explicit_alignment;

        if (!parse_record_suffix_attributes(parser, record_id, &explicit_alignment)) {
            return false;
        }
        if (explicit_alignment != 0U) {
            parser->program->records[record_id].explicit_alignment = explicit_alignment;
        }
    }
''',
    '''    if (!parse_record_type_attributes(parser, is_union, &explicit_alignment, &is_packed)) {
        return false;
    }
    parser->program->records[record_id].is_packed =
        parser->program->records[record_id].is_packed || is_packed;
    if (explicit_alignment > parser->program->records[record_id].explicit_alignment) {
        parser->program->records[record_id].explicit_alignment = explicit_alignment;
    }
''',
)

# Freeze both GNU placements for whole-record packed.
replace_once(
    "tests/compiler/c0/packed_record_layout.c",
    '''struct __attribute__((__packed__)) packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
};

static struct packed_sample sample;
''',
    '''struct __attribute__((__packed__)) packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
};

struct suffix_packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
} __attribute__((__packed__));

struct forward_packed_sample;
struct __attribute__((__packed__)) forward_packed_sample {
    unsigned char first;
    unsigned short second;
};

static struct packed_sample sample;
static struct suffix_packed_sample suffix_sample;
static struct forward_packed_sample forward_sample;
''',
)

replace_once(
    "tests/compiler/c0/packed_record_layout.c",
    '''int main(void) {
    return sizeof(struct packed_sample) == 4 ? 0 : 1;
}
''',
    '''int main(void) {
    return sizeof(struct packed_sample) == 4 && sizeof(struct suffix_packed_sample) == 4 &&
                   sizeof(struct forward_packed_sample) == 3
               ? 0
               : 1;
}
''',
)

replace_once(
    "tests/compiler/c0/run-packed-record-layout.sh",
    '''grep -F '.size sample, 4' "$work/packed_record_layout.s" >/dev/null
grep -F '  addi a0, a0, 1' "$work/packed_record_layout.s" >/dev/null
''',
    '''grep -F '.size sample, 4' "$work/packed_record_layout.s" >/dev/null
grep -F '.size suffix_sample, 4' "$work/packed_record_layout.s" >/dev/null
grep -F '.size forward_sample, 3' "$work/packed_record_layout.s" >/dev/null
grep -F '  addi a0, a0, 1' "$work/packed_record_layout.s" >/dev/null
''',
)
replace_once(
    "tests/compiler/c0/run-packed-record-layout.sh",
    "printf '%s\\n' 'PASS compiler/c0/packed_record_layout size=4 offsets=0,1,3 alignment=1'\n",
    "printf '%s\\n' 'PASS compiler/c0/packed_record_layout placement=prefix+suffix forward-definition=1 size=4 offsets=0,1,3 alignment=1'\n",
)

# The same TYPE consumer now owns prefix aligned as well as the existing suffix form.
replace_once(
    "tests/compiler/c0/gnu_record_alignment.c",
    '''struct PointerAligned {
    char byte;
} __attribute__((aligned(sizeof(void *))));

''',
    '''struct PointerAligned {
    char byte;
} __attribute__((aligned(sizeof(void *))));

struct __attribute__((aligned(16))) PrefixAligned {
    char byte;
};

''',
)
replace_once(
    "tests/compiler/c0/gnu_record_alignment.c",
    '''unsigned long pointer_aligned_size(void) {
    return sizeof(struct PointerAligned);
}

''',
    '''unsigned long pointer_aligned_size(void) {
    return sizeof(struct PointerAligned);
}

unsigned long prefix_aligned_size(void) {
    return sizeof(struct PrefixAligned);
}

''',
)
replace_once(
    "tests/compiler/c0/run-gnu-record-alignment.sh",
    '''for symbol in pointer_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset designated_only_size designated_aligned_size; do
''',
    '''for symbol in pointer_aligned_size prefix_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset designated_only_size designated_aligned_size; do
''',
)
replace_once(
    "tests/compiler/c0/run-gnu-record-alignment.sh",
    '''test "$size16" -ge 2
''',
    '''test "$size16" -ge 3
''',
)
replace_once(
    "tests/compiler/c0/run-gnu-record-alignment.sh",
    "printf '%s\\n' 'PASS compiler/c0/gnu_record_alignment sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-alignment-decoder=1 typed-alignment-consteval=1 designated-init=diagnostic-struct-only mixed-suffix=1'\n",
    "printf '%s\\n' 'PASS compiler/c0/gnu_record_alignment placement=prefix+suffix sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-type-attribute-consumer=1 typed-alignment-consteval=1 designated-init=diagnostic-struct-only mixed-suffix=1'\n",
)
