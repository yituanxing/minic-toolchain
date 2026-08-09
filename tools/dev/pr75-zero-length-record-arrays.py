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
# zero storage while retaining the element type's natural alignment.
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

# Earlier stages already expose the shared integer constant-expression parser. Add a
# record-member entry point that differs only in allowing GNU bound zero.
core_path = Path("src/frontend/parser_core.c")
text = core_path.read_text()
signature = "bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {"
start = text.find(signature)
if start < 0:
    raise SystemExit("parser_core.c: cannot locate fixed array bound function")
end = text.find("\n}\n", start)
if end < 0:
    raise SystemExit("parser_core.c: cannot locate fixed array bound function end")
end += len("\n}\n")
helper = r'''
bool minic_parser_parse_record_array_bound(MinicParser *parser,
                                           size_t *element_count,
                                           bool *is_zero_length) {
    int64_t value;

    if (element_count == NULL || is_zero_length == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &value)) {
        return false;
    }
    if (value < 0) {
        minic_parser_error(parser, "record array bound must not be negative");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "record array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *is_zero_length = value == 0;
    *element_count = value == 0 ? 1U : (size_t)value;
    return true;
}
'''
text = text[:end] + helper + text[end:]
core_path.write_text(text)

# parser_record.c has already been expanded to support multidimensional fields. Only the
# outermost explicit dimension may carry GNU zero length; inner dimensions stay ordinary
# positive array types. A zero outer dimension is represented by element_count=1 plus a flag.
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
replace_once(
    "src/frontend/parser_record.c",
    """            if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
                return false;
            }
            bound_count += 1U;
""",
    """            if (bound_count == 0U) {
                if (!minic_parser_parse_record_array_bound(
                        parser, &bounds[bound_count], &is_zero_length_array)) {
                    return false;
                }
            } else if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
                return false;
            }
            bound_count += 1U;
""",
)
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

# Final RV64 layout: zero storage, normal element alignment.
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
