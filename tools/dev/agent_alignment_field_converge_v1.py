#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_record.c"
text = path.read_text()

pattern = r'''static bool parse_record_field_alignment_attribute\(MinicParser \*parser, size_t \*alignment\) \{.*?\n\}\n\n'''
replacement = '''typedef struct MinicRecordFieldAttributeContext {
    size_t explicit_alignment;
} MinicRecordFieldAttributeContext;

static bool consume_record_field_attribute(MinicParser *parser,
                                           const MinicParsedAttribute *attribute,
                                           void *opaque_context) {
    MinicRecordFieldAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicRecordFieldAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FIELD)) {
        minic_parser_error(parser, "unsupported GNU record field attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "record field", &context->explicit_alignment);
    }
    minic_parser_error(parser, "unsupported GNU record field attribute");
    return false;
}

static bool parse_record_field_attributes(MinicParser *parser, size_t *explicit_alignment) {
    MinicRecordFieldAttributeContext context;

    if (parser == NULL || explicit_alignment == NULL) {
        return false;
    }
    context.explicit_alignment = 0U;
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_record_field_attribute, &context)) {
        return false;
    }
    *explicit_alignment = context.explicit_alignment;
    return true;
}

'''
text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
if count != 1:
    raise SystemExit(f"record field alignment parser replacement: expected 1, found {count}")
old = '''    if (!parse_record_field_alignment_attribute(parser, &explicit_alignment)) {
        return false;
    }
'''
new = '''    if (!parse_record_field_attributes(parser, &explicit_alignment)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"record field attribute call replacement: expected 1, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
