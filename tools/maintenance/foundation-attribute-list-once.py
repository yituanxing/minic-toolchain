#!/usr/bin/env python3
"""One-shot exact-marker migration for the shared GNU AttributeList parser."""

from pathlib import Path


def update_makefile() -> None:
    path = Path("Makefile")
    text = path.read_text()
    source = "\tsrc/frontend/parser_attribute.c \\\n"
    if source in text:
        return
    anchor = "\tsrc/frontend/parser_constant.c \\\n"
    if text.count(anchor) != 1:
        raise SystemExit("Makefile parser source anchor is not unique")
    path.write_text(text.replace(anchor, anchor + source, 1))


def replace_unique_range(text: str,
                         start_marker: str,
                         end_marker: str,
                         replacement: str,
                         label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: migration markers missing")
    if text.find(start_marker, start + 1) >= 0:
        raise SystemExit(f"{label}: start marker is not unique")
    return text[:start] + replacement + text[end:]


def migrate_function_attributes() -> None:
    path = Path("src/frontend/parser_function.c")
    text = path.read_text()

    shared_helpers = r'''typedef struct MinicFunctionAttributeContext {
    bool allow_gnu_inline;
    bool is_internal;
    bool is_inline;
    const char *unsupported_message;
} MinicFunctionAttributeContext;

static bool function_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool consume_function_attribute(MinicParser *parser,
                                       const MinicParsedAttribute *attribute,
                                       void *opaque_context) {
    const MinicFunctionAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (const MinicFunctionAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)) {
        minic_parser_error(parser, "%s", context->unsupported_message);
        return false;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {
        if (!context->allow_gnu_inline) {
            minic_parser_error(parser, "%s", context->unsupported_message);
            return false;
        }
        /* GNU inline changes external-inline linkage semantics. Linux's current
         * accepted placement is static inline, where this parse-only attribute
         * does not change externally visible linkage. */
        if (!context->is_internal || !context->is_inline) {
            minic_parser_error(
                parser, "GNU gnu_inline requires explicit non-static inline semantics");
            return false;
        }
        return true;
    }

    if (!function_attribute_class_is_parse_only(descriptor->semantic_class)) {
        minic_parser_error(parser, "%s", context->unsupported_message);
        return false;
    }
    return true;
}

static bool parse_function_attribute_lists(MinicParser *parser,
                                           bool allow_gnu_inline,
                                           bool is_internal,
                                           bool is_inline,
                                           const char *unsupported_message) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
}

'''

    text = replace_unique_range(
        text,
        "static bool gnu_function_attribute_is_metadata(const MinicParser *parser) {",
        "static bool section_attribute_token_is(const MinicParser *parser, const char *name) {",
        shared_helpers,
        "function attribute helpers",
    )

    shared_entry_points = r'''bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {
    return parse_function_attribute_lists(
        parser,
        false,
        false,
        false,
        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "
        "implemented explicitly");
}

static bool parse_gnu_predeclarator_function_attributes(MinicParser *parser) {
    return minic_parser_parse_gnu_function_attributes(parser);
}

bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                       bool is_internal,
                                                       bool is_inline) {
    return parse_function_attribute_lists(
        parser,
        true,
        is_internal,
        is_inline,
        "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be "
        "implemented explicitly");
}

'''

    text = replace_unique_range(
        text,
        "bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {",
        "static bool parse_gnu_function_asm_label(",
        shared_entry_points,
        "function attribute entry points",
    )
    path.write_text(text)


def extend_attribute_gate() -> None:
    path = Path("tests/compiler/c0/gnu_function_attributes.c")
    text = path.read_text()
    declaration = "extern int stable_transform(int value) __attribute__((const));\n\n"
    if declaration not in text:
        anchor = "extern void __attribute__((noreturn)) fatal_error(void);\n"
        if text.count(anchor) != 1:
            raise SystemExit("function attribute fixture declaration anchor mismatch")
        text = text.replace(anchor, declaration + anchor, 1)
    old_return = "    return allocated != (void *)0 && memory_compare(destination, source, 4);"
    new_return = (
        "    return allocated != (void *)0 && memory_compare(destination, source, 4) &&\n"
        "           stable_transform(1);"
    )
    if old_return in text:
        text = text.replace(old_return, new_return, 1)
    elif "stable_transform(1)" not in text:
        raise SystemExit("function attribute fixture return anchor mismatch")
    path.write_text(text)

    runner = Path("tests/compiler/c0/run-gnu-function-attributes.sh")
    text = runner.read_text()
    call_anchor = "grep -F '  call memory_compare' \"$work/gnu_function_attributes.s\" >/dev/null\n"
    if "call stable_transform" not in text:
        if text.count(call_anchor) != 1:
            raise SystemExit("function attribute runner call anchor mismatch")
        text = text.replace(
            call_anchor,
            call_anchor
            + "grep -F '  call stable_transform' \"$work/gnu_function_attributes.s\" >/dev/null\n",
            1,
        )
    old_pass = "metadata=nothrow,leaf,nonnull,access,pure,malloc,noreturn,deprecated placement=pre-declarator,suffix unknown=reject aligned=not-silently-ignored"
    new_pass = "metadata=nothrow,leaf,nonnull,access,pure,malloc,noreturn,deprecated,const-keyword placement=pre-declarator,suffix unknown=reject aligned=not-silently-ignored"
    if old_pass in text:
        text = text.replace(old_pass, new_pass, 1)
    elif new_pass not in text:
        raise SystemExit("function attribute runner PASS anchor mismatch")
    runner.write_text(text)


def main() -> None:
    update_makefile()
    migrate_function_attributes()
    extend_attribute_gate()
    print("shared GNU AttributeList migration applied")


if __name__ == "__main__":
    main()
