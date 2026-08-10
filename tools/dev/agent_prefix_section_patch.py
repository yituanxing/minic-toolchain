#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Deferred declaration attributes already preserve descriptor identity and the
# exact argument source span. Consume only the object semantics we already know
# how to represent, rather than forcing prefix placement through a parse-only
# validator or inventing a second section representation.
start = '''static bool validate_object_attribute_list(MinicParser *parser,
                                           const MinicParsedAttributeList *attributes) {
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        const MinicAttributeDescriptor *descriptor = attributes->values[index].descriptor;

        if (descriptor == NULL ||
            !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT) ||
            !function_attribute_class_is_parse_only(descriptor->semantic_class)) {
            minic_parser_error(parser,
                               "unsupported GNU object prefix attribute; symbol/layout "
                               "attributes require "
                               "explicit object semantics");
            return false;
        }
    }
    return true;
}
'''
replacement = r'''static bool decode_deferred_section_argument(MinicParser *parser,
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
        while (cursor < end &&
               (parser->source[cursor] == ' ' || parser->source[cursor] == '\t' ||
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

static bool apply_object_attribute_list(MinicParser *parser,
                                        const MinicParsedAttributeList *attributes,
                                        char *section_name,
                                        size_t section_capacity,
                                        size_t *section_name_length,
                                        bool *has_section) {
    size_t index;

    if (parser == NULL || attributes == NULL || section_name == NULL ||
        section_name_length == NULL || has_section == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        const MinicParsedAttribute *attribute;
        const MinicAttributeDescriptor *descriptor;

        attribute = &attributes->values[index];
        descriptor = attribute->descriptor;
        if (descriptor == NULL ||
            !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
            minic_parser_error(parser, "unsupported GNU object prefix attribute");
            return false;
        }
        if (function_attribute_class_is_parse_only(descriptor->semantic_class)) {
            continue;
        }
        if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
            if (!decode_deferred_section_argument(parser,
                                                  attribute,
                                                  section_name,
                                                  section_capacity,
                                                  section_name_length,
                                                  has_section)) {
                return false;
            }
            continue;
        }
        minic_parser_error(parser,
                           "unsupported GNU object prefix attribute; symbol/layout attributes "
                           "require explicit object semantics");
        return false;
    }
    return true;
}
'''
replace_once("src/frontend/parser_function.c", start, replacement, "deferred-object-attribute-consumer")

# Both object dispatches now apply supported deferred prefix semantics into the
# same section state already consumed by parser_global. Static objects still hit
# their existing explicit-symbol-semantics boundary after this application.
old_call = '''        if (!validate_object_attribute_list(parser, &deferred_attributes)) {
            return false;
        }
'''
new_call = '''        if (!apply_object_attribute_list(parser,
                                         &deferred_attributes,
                                         section_name,
                                         sizeof(section_name),
                                         &section_name_length,
                                         &has_section)) {
            return false;
        }
'''
p = Path("src/frontend/parser_function.c")
text = p.read_text()
if text.count(old_call) != 2:
    raise SystemExit(f"object attribute dispatch: expected two anchors, found {text.count(old_call)}")
p.write_text(text.replace(old_call, new_call))

# asm-goto v0 deliberately has no register operands. A branch from the template
# can bypass code after the asm, so it must not own a temporary operand stack.
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    '''    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (!constraint_is(operand, "r") && !(inline_asm->is_goto && constraint_is(operand, "i")))) {
        return false;
    }
''',
    '''    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (inline_asm->is_goto ? !constraint_is(operand, "i") : !constraint_is(operand, "r"))) {
        return false;
    }
''',
    "asm-goto-immediate-only",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    '''    if (operand_count > (SIZE_MAX - 15U) / 8U) {
        return false;
    }
    temporary_size = (operand_count * 8U + 15U) & ~(size_t)15U;
''',
    '''    if (operand_count > (SIZE_MAX - 15U) / 8U) {
        return false;
    }
    temporary_size = inline_asm->is_goto ? 0U : (operand_count * 8U + 15U) & ~(size_t)15U;
''',
    "asm-goto-zero-temp-stack",
)
