#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
attribute_h = root / "src/frontend/attribute.h"
attribute_c = root / "src/frontend/attribute.c"
enum_c = root / "src/frontend/parser_enum.c"
run_sh = root / "tests/compiler/c0/run.sh"

text = attribute_h.read_text()
old = "    MINIC_ATTRIBUTE_DESIGNATED_INIT,\n    MINIC_ATTRIBUTE_PACKED,\n"
new = "    MINIC_ATTRIBUTE_DESIGNATED_INIT,\n    MINIC_ATTRIBUTE_MODE,\n    MINIC_ATTRIBUTE_PACKED,\n"
if new not in text:
    if old not in text:
        raise SystemExit("attribute kind anchor changed")
    text = text.replace(old, new, 1)
attribute_h.write_text(text)

text = attribute_c.read_text()
anchor = '''    MINIC_ATTRIBUTE_ENTRY("packed",
'''
descriptors = '''    {
        "mode",
        sizeof("mode") - 1U,
        MINIC_ATTRIBUTE_MODE,
        MINIC_ATTRIBUTE_CLASS_LAYOUT,
        MINIC_ATTRIBUTE_TARGET_TYPE,
        1U,
        1U,
        true,
    },
    {
        "__mode__",
        sizeof("__mode__") - 1U,
        MINIC_ATTRIBUTE_MODE,
        MINIC_ATTRIBUTE_CLASS_LAYOUT,
        MINIC_ATTRIBUTE_TARGET_TYPE,
        1U,
        1U,
        true,
    },
'''
if descriptors not in text:
    if anchor not in text:
        raise SystemExit("attribute descriptor anchor changed")
    text = text.replace(anchor, descriptors + anchor, 1)
attribute_c.write_text(text)

text = enum_c.read_text()
function_anchor = "bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type) {\n"
helper = r'''typedef struct MinicEnumAttributeContext {
    bool has_byte_mode;
} MinicEnumAttributeContext;

static bool enum_mode_argument_is_byte(const MinicParser *parser,
                                       const MinicParsedAttribute *attribute) {
    size_t begin;
    size_t end;
    const char *value;
    size_t length;

    if (parser == NULL || attribute == NULL || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    begin = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    while (begin < end &&
           (parser->source[begin] == ' ' || parser->source[begin] == '\t' ||
            parser->source[begin] == '\n' || parser->source[begin] == '\r' ||
            parser->source[begin] == '\f' || parser->source[begin] == '\v')) {
        begin += 1U;
    }
    while (end > begin &&
           (parser->source[end - 1U] == ' ' || parser->source[end - 1U] == '\t' ||
            parser->source[end - 1U] == '\n' || parser->source[end - 1U] == '\r' ||
            parser->source[end - 1U] == '\f' || parser->source[end - 1U] == '\v')) {
        end -= 1U;
    }
    value = parser->source + begin;
    length = end - begin;
    return (length == 4U && memcmp(value, "byte", 4U) == 0) ||
           (length == 8U && memcmp(value, "__byte__", 8U) == 0);
}

static bool consume_enum_attribute(MinicParser *parser,
                                   const MinicParsedAttribute *attribute,
                                   void *opaque_context) {
    MinicEnumAttributeContext *context;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicEnumAttributeContext *)opaque_context;
    if (attribute->descriptor == NULL ||
        !minic_attribute_allowed_on(attribute->descriptor, MINIC_ATTRIBUTE_TARGET_TYPE) ||
        attribute->descriptor->kind != MINIC_ATTRIBUTE_MODE) {
        minic_parser_error(parser, "unsupported GNU enum type attribute");
        return false;
    }
    if (!enum_mode_argument_is_byte(parser, attribute)) {
        minic_parser_error(parser, "unsupported GNU enum mode; only mode(byte) is implemented");
        return false;
    }
    context->has_byte_mode = true;
    return true;
}

'''
if helper not in text:
    if function_anchor not in text:
        raise SystemExit("enum specifier anchor changed")
    text = text.replace(function_anchor, helper + function_anchor, 1)

old_vars = '''    bool has_next;
    bool has_tag;
    bool saw_negative;
'''
new_vars = '''    bool has_next;
    bool has_tag;
    bool saw_negative;
    MinicEnumAttributeContext attribute_context;
'''
if new_vars not in text:
    if old_vars not in text:
        raise SystemExit("enum local variable anchor changed")
    text = text.replace(old_vars, new_vars, 1)

old_tail = r'''    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    {
        MinicType compatible_type;

        if (!choose_enum_compatible_type(
                parser, saw_negative, minimum, maximum, &compatible_type) ||
            !minic_c0_program_finish_enum(parser->program, enum_id, compatible_type)) {
            minic_parser_error(parser,
                               "enum values do not fit a supported compatible integer type");
            return false;
        }
        *enum_type = minic_type_enum(enum_id);
        return !minic_type_is_void(*enum_type);
    }
'''
new_tail = r'''    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    (void)memset(&attribute_context, 0, sizeof(attribute_context));
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_enum_attribute, &attribute_context)) {
        return false;
    }
    {
        MinicType compatible_type;
        bool type_fits;

        if (attribute_context.has_byte_mode) {
            compatible_type = saw_negative ? minic_type_signed_char() : minic_type_unsigned_char();
            type_fits = saw_negative ? signed_type_fits(parser, compatible_type, minimum, maximum)
                                     : unsigned_type_fits(parser, compatible_type, maximum);
            if (!type_fits) {
                minic_parser_error(parser, "enum mode(byte) cannot represent enumerator values");
                return false;
            }
        } else if (!choose_enum_compatible_type(
                       parser, saw_negative, minimum, maximum, &compatible_type)) {
            minic_parser_error(parser,
                               "enum values do not fit a supported compatible integer type");
            return false;
        }
        if (!minic_c0_program_finish_enum(parser->program, enum_id, compatible_type)) {
            minic_parser_error(parser, "cannot finish enum compatible integer type");
            return false;
        }
        *enum_type = minic_type_enum(enum_id);
        return !minic_type_is_void(*enum_type);
    }
'''
if new_tail not in text:
    if old_tail not in text:
        raise SystemExit("enum finish anchor changed")
    text = text.replace(old_tail, new_tail, 1)
enum_c.write_text(text)

run = run_sh.read_text()
gate = '''
MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-enum-mode-byte.sh"
'''
if gate.strip() not in run:
    if not run.endswith("\n"):
        run += "\n"
    run += gate
run_sh.write_text(run)
print("materialized GNU enum mode(byte) semantics")
