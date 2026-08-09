#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:160]!r}")
    target.write_text(text.replace(old, new, 1))


# Keep zero-length arrays distinct from incomplete/flexible arrays. element_count remains one
# internally so existing array/field invariants stay valid; layout alone gives this GNU member
# zero storage while retaining the element type's alignment.
replace_once(
    "src/frontend/ast.h",
    """    bool is_array;
    bool is_flexible_array;
} MinicRecordField;
""",
    """    bool is_array;
    bool is_flexible_array;
    bool is_zero_length_array;
} MinicRecordField;
""",
)

replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
""",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
bool minic_parser_parse_record_array_bound(MinicParser *parser,
                                           size_t *element_count,
                                           bool *is_zero_length);
""",
)

# Share the same constant-expression parser; only record members may accept GNU bound zero.
replace_once(
    "src/frontend/parser_core.c",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
        return false;
    }
    if (value <= 0) {
        minic_parser_error(parser, "array bound must be greater than zero");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *element_count = (size_t)value;
    return true;
}
""",
    """static bool parse_checked_array_bound(MinicParser *parser,
                                      size_t *element_count,
                                      bool allow_zero,
                                      bool *is_zero_length) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
        return false;
    }
    if (value < 0 || (!allow_zero && value == 0)) {
        minic_parser_error(parser,
                           allow_zero ? "array bound must not be negative"
                                      : "array bound must be greater than zero");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    if (is_zero_length != NULL) {
        *is_zero_length = value == 0;
    }
    *element_count = value == 0 ? 1U : (size_t)value;
    return true;
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    return parse_checked_array_bound(parser, element_count, false, NULL);
}

bool minic_parser_parse_record_array_bound(MinicParser *parser,
                                           size_t *element_count,
                                           bool *is_zero_length) {
    if (is_zero_length == NULL) {
        return false;
    }
    *is_zero_length = false;
    return parse_checked_array_bound(parser, element_count, true, is_zero_length);
}
""",
)

# parser_record.c has already been expanded by earlier Lua staging into the shared declarator
# helper. Add one flag and use the record-only bound parser there.
replace_once(
    "src/frontend/parser_record.c",
    """    bool is_array;
    bool is_flexible_array;
""",
    """    bool is_array;
    bool is_flexible_array;
    bool is_zero_length_array;
""",
)
replace_once(
    "src/frontend/parser_record.c",
    """    is_array = false;
    is_flexible_array = false;
""",
    """    is_array = false;
    is_flexible_array = false;
    is_zero_length_array = false;
""",
)
# The multidimensional staging leaves the innermost explicit bound on this shared call.
record_path = Path("src/frontend/parser_record.c")
text = record_path.read_text()
old = "minic_parser_parse_fixed_array_bound(parser, &element_count)"
count = text.count(old)
if count < 1:
    raise SystemExit("parser_record.c: no fixed record array bound call found")
# Only the first call is the direct field bound; nested suffix dimensions remain ordinary
# positive array types because a zero inner dimension would require a different type model.
text = text.replace(
    old,
    "minic_parser_parse_record_array_bound(parser, &element_count, &is_zero_length_array)",
    1,
)
record_path.write_text(text)

replace_once(
    "src/frontend/parser_record.c",
    """    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = is_flexible_array;
""",
    """    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = is_flexible_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_zero_length_array =
        is_zero_length_array;
""",
)

# Final RV64 layout: zero storage, normal element alignment. This intentionally differs from
# flexible arrays only in parsing/placement rules; both occupy zero bytes.
replace_once(
    "src/target/riscv64/layout.c",
    """        field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
""",
    """        field_size = (field->is_flexible_array || field->is_zero_length_array)
                         ? 0U
                         : element_size * field->element_count;
""",
)

# Keep both parse-time target-layout copies identical to final RV64 layout.
core = Path("src/frontend/parser_core.c")
text = core.read_text()
old = "field_size = field->is_flexible_array ? 0U : element_size * field->element_count;"
count = text.count(old)
if count != 2:
    raise SystemExit(f"parser_core.c: expected 2 constant-layout field-size sites, found {count}")
text = text.replace(
    old,
    "field_size = (field->is_flexible_array || field->is_zero_length_array)\n"
    "                             ? 0U\n"
    "                             : element_size * field->element_count;",
)
core.write_text(text)

print("staged GNU zero-length record array members with zero storage and natural alignment")
