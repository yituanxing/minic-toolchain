#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_in_function(path: str, signature: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"{label}: missing {signature}")
    candidates = [
        pos
        for pos in (
            text.find("\nstatic ", start + len(signature)),
            text.find("\nbool ", start + len(signature)),
            text.find("\nvoid ", start + len(signature)),
        )
        if pos >= 0
    ]
    end = min(candidates) if candidates else len(text)
    body = text[start:end]
    count = body.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor in function, found {count}")
    target.write_text(text[:start] + body.replace(old, new, 1) + text[end:])


# Expose the already shared fixed-array integer-constant-expression engine to other
# declaration semantics.  This deliberately avoids creating a second sizeof/enum/cast
# evaluator for GNU attributes.
replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
""",
    """bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);
bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
""",
    "constant-expression-prototype",
)
replace_once(
    "src/frontend/parser_core.c",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
""",
    """bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    return parse_array_bound_additive(parser, value);
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &value)) {
""",
    "constant-expression-wrapper",
)

# Record alignment is semantic declaration metadata.  It is kept distinct from the
# computed target layout so a later TargetLayout side table can move the latter without
# losing the source-level attribute.
replace_once(
    "src/frontend/ast.h",
    """    size_t storage_size;
    size_t alignment;
    bool is_union;
""",
    """    size_t storage_size;
    size_t alignment;
    size_t explicit_alignment;
    bool is_union;
""",
    "record-explicit-alignment-field",
)

path = Path("src/frontend/parser_record.c")
text = path.read_text()
marker = "static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {\n"
helper = r'''static bool parse_record_suffix_alignment(MinicParser *parser, size_t *alignment) {
    int64_t value;

    if (parser == NULL || alignment == NULL) {
        return false;
    }
    *alignment = 0U;
    if (!token_text_equals(parser, parser->current, "__attribute__") &&
        !token_text_equals(parser, parser->current, "__attribute")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after record __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' in record __attribute__")) {
        return false;
    }
    if (!token_text_equals(parser, parser->current, "aligned") &&
        !token_text_equals(parser, parser->current, "__aligned__")) {
        minic_parser_error(parser, "unsupported GNU record suffix attribute");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after record aligned") ||
        !minic_parser_parse_integer_constant_expression(parser, &value)) {
        return false;
    }
    if (value <= 0 || (uint64_t)value > (uint64_t)SIZE_MAX ||
        (((uint64_t)value & ((uint64_t)value - UINT64_C(1))) != 0U)) {
        minic_parser_error(parser, "record alignment must be a positive power of two");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after record alignment") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in record attribute") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected second ')' in record attribute")) {
        return false;
    }
    *alignment = (size_t)value;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"record suffix helper marker: expected one match, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)

old = """    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields")) {
        return false;
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
"""
new = """    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields")) {
        return false;
    }
    {
        size_t explicit_alignment;

        if (!parse_record_suffix_alignment(parser, &explicit_alignment)) {
            return false;
        }
        if (explicit_alignment != 0U) {
            parser->program->records[record_id].explicit_alignment = explicit_alignment;
        }
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
"""
if text.count(old) != 1:
    raise SystemExit(f"record suffix parse anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

# Final RV64 record layout: explicit aligned(N) raises the aggregate alignment and hence
# rounds the aggregate size to N.  It can also raise a packed aggregate alignment; field
# packing remains controlled independently by is_packed/field attributes.
replace_in_function(
    "src/target/riscv64/layout.c",
    "static bool\nminic_riscv64_layout_one_record(",
    """    if (!minic_riscv64_align_up(storage_size, record_alignment, &record->storage_size)) {
""",
    """    if (record->explicit_alignment != 0U) {
        if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (record->explicit_alignment > record_alignment) {
            record_alignment = record->explicit_alignment;
        }
    }
    if (!minic_riscv64_align_up(storage_size, record_alignment, &record->storage_size)) {
""",
    "rv64-record-explicit-alignment",
)

# The shared constant-expression sizeof/offsetof layout must agree with final RV64 layout.
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_type_layout(",
    """        if (!constant_align_up(storage_size, record_alignment, size)) {
            return false;
        }
""",
    """        if (record->explicit_alignment != 0U) {
            if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if ((uint64_t)record->explicit_alignment > record_alignment) {
                record_alignment = (uint64_t)record->explicit_alignment;
            }
        }
        if (!constant_align_up(storage_size, record_alignment, size)) {
            return false;
        }
""",
    "constant-record-explicit-alignment",
)

print("staged GNU record suffix aligned(constant-expression) with shared ICE and RV64 layout")
