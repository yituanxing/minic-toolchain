from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact match, got {count}")
    return text.replace(old, new, 1)


def sub_once(text, pattern, replacement, label, flags=0):
    new, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, got {count}")
    return new


# Registry: designated_init is a zero-argument diagnostic attribute on struct types.
path = "src/frontend/attribute.h"
text = read(path)
text = replace_once(
    text,
    "    MINIC_ATTRIBUTE_VISIBILITY,\n    MINIC_ATTRIBUTE_PACKED,",
    "    MINIC_ATTRIBUTE_VISIBILITY,\n    MINIC_ATTRIBUTE_DESIGNATED_INIT,\n    MINIC_ATTRIBUTE_PACKED,",
    "attribute kind",
)
write(path, text)

path = "src/frontend/attribute.c"
text = read(path)
anchor = '''    MINIC_ATTRIBUTE_ENTRY("packed",
                          MINIC_ATTRIBUTE_PACKED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE),
'''
entries = '''    {
        "designated_init",
        sizeof("designated_init") - 1U,
        MINIC_ATTRIBUTE_DESIGNATED_INIT,
        MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
        MINIC_ATTRIBUTE_TARGET_TYPE,
        0U,
        0U,
        true,
    },
    {
        "__designated_init__",
        sizeof("__designated_init__") - 1U,
        MINIC_ATTRIBUTE_DESIGNATED_INIT,
        MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
        MINIC_ATTRIBUTE_TARGET_TYPE,
        0U,
        0U,
        true,
    },
'''
text = replace_once(text, anchor, entries + anchor, "designated-init descriptors")
write(path, text)

# Parser attribute service: preserve the legacy __attribute spelling and own alignment decoding.
path = "src/frontend/parser_internal.h"
text = read(path)
text = replace_once(
    text,
    "bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,\n"
    "                                              MinicParsedAttributeList *attributes);",
    "bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,\n"
    "                                              MinicParsedAttributeList *attributes);\n"
    "bool minic_parser_apply_alignment_attribute(MinicParser *parser,\n"
    "                                            const MinicParsedAttribute *attribute,\n"
    "                                            const char *subject,\n"
    "                                            size_t *explicit_alignment);",
    "shared alignment declaration",
)
write(path, text)

path = "src/frontend/parser_attribute.c"
text = read(path)
text = replace_once(
    text,
    '    while (parser_token_text_is(parser, parser->current, "__attribute__")) {',
    '    while (parser_token_text_is(parser, parser->current, "__attribute__") ||\n'
    '           parser_token_text_is(parser, parser->current, "__attribute")) {',
    "generic attribute spelling",
)
append_anchor = '''bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,
                                              MinicParsedAttributeList *attributes) {
    if (parser == NULL || attributes == NULL) {
        return false;
    }
    return minic_parser_parse_gnu_attribute_lists(parser, collect_parsed_attribute, attributes);
}
'''
helper = '''bool minic_parser_apply_alignment_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            const char *subject,
                                            size_t *explicit_alignment) {
    MinicParser probe;
    int64_t parsed_alignment;
    size_t alignment;

    if (parser == NULL || attribute == NULL || subject == NULL || explicit_alignment == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_integer_constant_expression(&probe, &parsed_alignment) ||
        probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser,
                               "GNU %s alignment requires one integer constant expression",
                               subject);
        }
        return false;
    }
    if (parsed_alignment <= 0 || (uint64_t)parsed_alignment > (uint64_t)SIZE_MAX) {
        minic_parser_error(
            parser, "GNU %s alignment must be a positive target-size value", subject);
        return false;
    }
    alignment = (size_t)parsed_alignment;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "GNU %s alignment must be a power of two", subject);
        return false;
    }
    if (alignment > *explicit_alignment) {
        *explicit_alignment = alignment;
    }
    return true;
}
'''
text = replace_once(text, append_anchor, append_anchor + "\n" + helper, "shared alignment helper")
write(path, text)

# Object attributes consume the shared alignment decoder; remove their private copy.
path = "src/frontend/parser_function.c"
text = read(path)
text = sub_once(
    text,
    r"static bool decode_deferred_alignment_argument\(MinicParser \*parser,.*?\n}\n\n(?=static bool apply_object_attribute_list)",
    "",
    "remove private alignment decoder",
    flags=re.S,
)
text = replace_once(
    text,
    "            if (!decode_deferred_alignment_argument(parser, attribute, explicit_alignment)) {",
    "            if (!minic_parser_apply_alignment_attribute(\n"
    "                    parser, attribute, \"object\", explicit_alignment)) {",
    "object shared alignment use",
)
write(path, text)

# Record suffix attributes use the generic registry/consumer path instead of a dedicated aligned parser.
path = "src/frontend/parser_record.c"
text = read(path)
replacement = '''typedef struct MinicRecordSuffixAttributeContext {
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
text = sub_once(
    text,
    r"static bool parse_record_suffix_alignment\(MinicParser \*parser, size_t \*alignment\) \{.*?\n}\n\n(?=static bool parse_record_field)",
    replacement + "\n",
    "record suffix consumer",
    flags=re.S,
)
text = replace_once(
    text,
    "        if (!parse_record_suffix_alignment(parser, &explicit_alignment)) {",
    "        if (!parse_record_suffix_attributes(parser, record_id, &explicit_alignment)) {",
    "record suffix call",
)
write(path, text)

# Strengthen the existing record-alignment focused contract with designated_init and mixed suffix lists.
path = "tests/compiler/c0/gnu_record_alignment.c"
text = read(path)
text += '''
struct DesignatedOnly {
    int first;
    int second;
} __attribute__((__designated_init__));

struct DesignatedAligned {
    char byte;
} __attribute__((__designated_init__, aligned(16)));

unsigned long designated_only_size(void) {
    return sizeof(struct DesignatedOnly);
}

unsigned long designated_aligned_size(void) {
    return sizeof(struct DesignatedAligned);
}
'''
write(path, text)

write(
    "tests/compiler/c0/invalid_record_designated_init_arguments.c",
    '''struct BadDesignated {
    int value;
} __attribute__((__designated_init__(1)));
''',
)
write(
    "tests/compiler/c0/invalid_union_designated_init.c",
    '''union BadDesignatedUnion {
    int value;
} __attribute__((__designated_init__));
''',
)

path = "tests/compiler/c0/run-gnu-record-alignment.sh"
text = read(path)
text = replace_once(
    text,
    "for symbol in pointer_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset; do",
    "for symbol in pointer_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset designated_only_size designated_aligned_size; do",
    "record symbols",
)
text = replace_once(
    text,
    "test \"$size32\" -ge 1\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_record_alignment sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-ice=1'",
    "test \"$size32\" -ge 1\n\n"
    "sed -n '/designated_only_size:/,/^\\.size/p' \"$assembly\" | grep -F '  li a0, 8' >/dev/null\n"
    "sed -n '/designated_aligned_size:/,/^\\.size/p' \"$assembly\" | grep -F '  li a0, 16' >/dev/null\n\n"
    "for invalid in invalid_record_designated_init_arguments invalid_union_designated_init; do\n"
    "    \"$host_cc\" -E -P -std=gnu11 -x c \"$root/tests/compiler/c0/$invalid.c\" -o \"$work/$invalid.i\"\n"
    "    if \"$minic\" -S \"$work/$invalid.i\" -o \"$work/$invalid.s\" >\"$work/$invalid.out\" 2>\"$work/$invalid.err\"; then\n"
    "        printf '%s\\n' \"expected $invalid to fail\" >&2\n"
    "        exit 1\n"
    "    fi\n"
    "done\n"
    "grep -F 'GNU attribute has an invalid number of arguments' \"$work/invalid_record_designated_init_arguments.err\" >/dev/null\n"
    "grep -F 'GNU designated_init applies only to struct types' \"$work/invalid_union_designated_init.err\" >/dev/null\n\n"
    "printf '%s\\n' 'PASS compiler/c0/gnu_record_alignment sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-alignment-decoder=1 designated-init=diagnostic-struct-only mixed-suffix=1'",
    "record runner upgrade",
)
write(path, text)

print("PASS generated record suffix attribute convergence slice")
