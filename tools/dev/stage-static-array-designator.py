#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S | re.M)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}: {pattern[:120]!r}")
    p.write_text(text)


# One typed-ConstEval owner for C99/GNU array designator bounds. Runtime
# initialization adds its forward-only execution restriction on top; static
# initialization is allowed to overwrite earlier slots as required by C.
header_anchor = "bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);\n"
header_add = header_anchor + """bool minic_parser_parse_array_designator(MinicParser *parser,\n                                         size_t element_count,\n                                         bool infer_bound,\n                                         size_t *first,\n                                         size_t *last);\n"""
replace_once("src/frontend/parser_internal.h", header_anchor, header_add)

constant_append = r'''

static bool parse_array_designator_bound(MinicParser *parser,
                                         size_t element_count,
                                         bool infer_bound,
                                         size_t *bound) {
    MinicConstValue constant;
    MinicExpressionId expression_id;
    int64_t value;

    if (parser == NULL || bound == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U) ||
        !minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &value)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "array designator requires an integer constant expression");
        }
        return false;
    }
    if (value < 0 || (uint64_t)value > (uint64_t)SIZE_MAX ||
        (!infer_bound && (uint64_t)value >= (uint64_t)element_count)) {
        minic_parser_error(parser, "array designator index is outside the initialized array");
        return false;
    }
    *bound = (size_t)value;
    return true;
}

bool minic_parser_parse_array_designator(MinicParser *parser,
                                         size_t element_count,
                                         bool infer_bound,
                                         size_t *first,
                                         size_t *last) {
    if (parser == NULL || first == NULL || last == NULL ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
        !parse_array_designator_bound(parser, element_count, infer_bound, first)) {
        return false;
    }
    *last = *first;
    if (parser->current.kind == MINIC_TOKEN_ELLIPSIS) {
        if (!minic_parser_advance(parser) ||
            !parse_array_designator_bound(parser, element_count, infer_bound, last)) {
            return false;
        }
        if (*last < *first) {
            minic_parser_error(parser,
                               "GNU array range designator upper bound is below lower bound");
            return false;
        }
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_RBRACKET,
                               "expected ']' after array designator") &&
           minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator");
}
'''
p = Path("src/frontend/parser_constant.c")
text = p.read_text()
if "bool minic_parser_parse_array_designator(" in text:
    raise SystemExit("parser_constant.c: array designator owner already exists")
p.write_text(text.rstrip() + constant_append + "\n")

# Runtime arrays now reuse the shared designator syntax/bound owner and keep
# only the execution-specific forward restriction locally.
regex_once(
    "src/frontend/parser_statement.c",
    r"static bool\nruntime_array_designator_bound\(.*?\n}\n\nstatic bool parse_runtime_array_designator\(",
    "static bool parse_runtime_array_designator(",
)
regex_once(
    "src/frontend/parser_statement.c",
    r"static bool parse_runtime_array_designator\(\n    MinicParser \*parser, size_t element_count, size_t next_index, size_t \*first, size_t \*last\) \{.*?\n}\n\nstatic bool runtime_array_multi_range_value_is_repeatable",
    r'''static bool parse_runtime_array_designator(
    MinicParser *parser, size_t element_count, size_t next_index, size_t *first, size_t *last) {
    if (!minic_parser_parse_array_designator(
            parser, element_count, false, first, last)) {
        return false;
    }
    if (*first < next_index) {
        minic_parser_error(parser, "backward runtime array designators are not supported yet");
        return false;
    }
    return true;
}

static bool runtime_array_multi_range_value_is_repeatable''',
)

# Parser-local static slot plan. It is deliberately transient: designator
# overwrite semantics are resolved before the canonical GlobalObject receives
# its final values/relocations, so no second persistent initializer IR exists.
p = Path("src/frontend/parser_global.c")
text = p.read_text()
slot_anchor = """typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    bool relocation_is_function;
    bool has_explicit_pointer_cast;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;
"""
slot_add = slot_anchor + """
typedef struct MinicStaticArraySlot {
    uint64_t integer_bits;
    MinicStaticPointerInitializer pointer_initializer;
} MinicStaticArraySlot;
"""
if text.count(slot_anchor) != 1:
    raise SystemExit("parser_global.c: static pointer initializer anchor not found uniquely")
text = text.replace(slot_anchor, slot_add, 1)
p.write_text(text)

plan_code = r'''
static bool grow_static_array_slots(MinicParser *parser,
                                    MinicStaticArraySlot **slots,
                                    size_t *capacity,
                                    size_t required) {
    MinicStaticArraySlot *resized;
    size_t old_capacity;
    size_t new_capacity;

    if (parser == NULL || slots == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }
    if (new_capacity < required || new_capacity > SIZE_MAX / sizeof(**slots)) {
        minic_parser_error(parser, "static array initializer slot count overflows");
        return false;
    }
    resized = (MinicStaticArraySlot *)realloc(*slots, new_capacity * sizeof(**slots));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while planning static array initializer");
        return false;
    }
    (void)memset(resized + old_capacity,
                 0,
                 (new_capacity - old_capacity) * sizeof(*resized));
    *slots = resized;
    *capacity = new_capacity;
    return true;
}

static bool parse_static_array_scalar_slot(MinicParser *parser,
                                           MinicType element_type,
                                           MinicStaticArraySlot *slot) {
    bool braced;

    if (parser == NULL || slot == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type))) {
        return false;
    }
    (void)memset(slot, 0, sizeof(*slot));
    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(element_type)) {
        if (!minic_parser_parse_integer_initializer_bits(
                parser, element_type, &slot->integer_bits)) {
            return false;
        }
    } else if (!parse_static_pointer_initializer(
                   parser, element_type, &slot->pointer_initializer)) {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser,
                               MINIC_TOKEN_RBRACE,
                               "expected '}' after scalar initializer");
}

static bool materialize_static_pointer_array_slot(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    size_t slot_index,
    const MinicStaticPointerInitializer *initializer) {
    bool recorded;

    if (parser == NULL || initializer == NULL ||
        !minic_c0_global_object_add_initializer_bits(
            parser->program, object_id, initializer->has_relocation ? 0U : initializer->bits)) {
        return false;
    }
    if (!initializer->has_relocation) {
        return true;
    }
    if (initializer->relocation_is_function) {
        recorded = initializer->has_explicit_pointer_cast
                       ? minic_c0_global_object_add_function_relocation_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->function_id)
                       : minic_c0_global_object_add_function_relocation(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->function_id);
    } else {
        recorded = initializer->has_explicit_pointer_cast
                       ? minic_c0_global_object_add_object_relocation_path_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->relocation_target.object_id,
                             initializer->relocation_target.member_indices,
                             initializer->relocation_target.member_depth)
                       : minic_c0_global_object_add_object_relocation_path(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->relocation_target.object_id,
                             initializer->relocation_target.member_indices,
                             initializer->relocation_target.member_depth);
    }
    return recorded;
}

static bool materialize_static_array_slots(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           MinicType element_type,
                                           const MinicStaticArraySlot *slots,
                                           size_t slot_count) {
    size_t index;

    if (parser == NULL || slots == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type))) {
        return false;
    }
    for (index = 0U; index < slot_count; ++index) {
        if (minic_type_is_integer(element_type)) {
            if (!minic_c0_global_object_add_initializer_bits(
                    parser->program, object_id, slots[index].integer_bits)) {
                minic_parser_error(parser, "cannot materialize static integer array slot");
                return false;
            }
        } else if (!materialize_static_pointer_array_slot(
                       parser, object_id, index, &slots[index].pointer_initializer)) {
            minic_parser_error(parser, "cannot materialize static pointer array slot");
            return false;
        }
    }
    return true;
}

static bool parse_static_scalar_array_plan(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           MinicType object_type,
                                           MinicType element_type,
                                           size_t element_count,
                                           bool infer_bound) {
    MinicStaticArraySlot *slots;
    size_t capacity;
    size_t extent;
    size_t next_index;
    bool success;

    slots = NULL;
    capacity = 0U;
    extent = infer_bound ? 0U : element_count;
    next_index = 0U;
    success = false;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        (!infer_bound && element_count == 0U) ||
        parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static scalar array initializer");
        }
        goto done;
    }
    if (!infer_bound &&
        !grow_static_array_slots(parser, &slots, &capacity, element_count)) {
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticArraySlot value;
        size_t first;
        size_t last;
        size_t required;
        size_t index;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last)) {
                goto done;
            }
        } else {
            first = next_index;
            last = first;
            if (!infer_bound && first >= element_count) {
                minic_parser_error(parser, "too many global array initializers");
                goto done;
            }
        }
        if (last == SIZE_MAX) {
            minic_parser_error(parser, "static array designator extent overflows");
            goto done;
        }
        required = last + 1U;
        if (infer_bound) {
            if (!grow_static_array_slots(parser, &slots, &capacity, required)) {
                goto done;
            }
            if (required > extent) {
                extent = required;
            }
        }
        if (!parse_static_array_scalar_slot(parser, element_type, &value)) {
            goto done;
        }
        for (index = first;; ++index) {
            slots[index] = value;
            if (index == last) {
                break;
            }
        }
        next_index = required;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(parser,
                             MINIC_TOKEN_RBRACE,
                             "expected '}' after static array initializer")) {
        goto done;
    }
    if (infer_bound) {
        if (extent == 0U) {
            minic_parser_error(parser, "cannot infer static array bound from an empty initializer");
            goto done;
        }
        if (!minic_c0_program_complete_array_type(parser->program, object_type, extent)) {
            minic_parser_error(parser, "cannot complete inferred static array type");
            goto done;
        }
    }
    if (!materialize_static_array_slots(
            parser, object_id, element_type, slots, extent)) {
        goto done;
    }
    success = true;

done:
    free(slots);
    return success;
}

'''
p = Path("src/frontend/parser_global.c")
text = p.read_text()
marker = "static bool parse_static_array_constant(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit("parser_global.c: static array constant marker not found uniquely")
text = text.replace(marker, plan_code + marker, 1)
p.write_text(text)

# The recursive static-storage initializer now uses the slot plan for scalar
# element arrays. Aggregate element arrays keep the existing recursive path.
regex_once(
    "src/frontend/parser_global.c",
    r"static bool parse_static_array_constant\(MinicParser \*parser,\n                                        MinicGlobalObjectId object_id,\n                                        const MinicArrayType \*array_type\) \{.*?\n}\n\nstatic bool parse_static_record_constant",
    r'''static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        MinicType type) {
    const MinicArrayType *array_type;
    MinicType element_type;
    size_t element_count;
    size_t element_index;

    array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
    if (array_type == NULL || array_type->element_count == 0U) {
        minic_parser_error(parser, "invalid complete static array initializer type");
        return false;
    }
    element_type = array_type->element_type;
    element_count = array_type->element_count;
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_array_plan(
            parser, object_id, type, element_type, element_count, false);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in array initializer")) {
        return false;
    }
    element_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (element_index >= element_count) {
            minic_parser_error(parser, "too many nested static array initializers");
            return false;
        }
        if (!minic_parser_parse_static_storage_initializer_value(
                parser, object_id, element_type)) {
            return false;
        }
        element_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static array initializer");
            return false;
        }
    }
    while (element_index < element_count) {
        if (!append_static_constant_zero(parser, object_id, element_type)) {
            minic_parser_error(parser, "cannot zero-fill nested static array initializer");
            return false;
        }
        element_index += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after array initializer");
}

static bool parse_static_record_constant''',
)
replace_once(
    "src/frontend/parser_global.c",
    """    if (minic_type_is_array(type)) {
        return parse_static_array_constant(
            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));
    }
""",
    """    if (minic_type_is_array(type)) {
        return parse_static_array_constant(parser, object_id, type);
    }
""",
)

# One-dimensional scalar static arrays now compose the canonical ArrayType,
# GlobalObject and shared static-storage initializer instead of integer/pointer
# special parsers. Multi-dimensional arrays remain on the established path.
static_array_helper = r'''
static bool static_scalar_array_is_single_dimension(MinicParser *parser, bool *single_dimension) {
    MinicParser probe;
    size_t ignored_count;

    if (parser == NULL || single_dimension == NULL ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(&probe, &ignored_count)) {
        return false;
    }
    *single_dimension = probe.current.kind != MINIC_TOKEN_LBRACKET;
    return true;
}

static bool parse_static_scalar_array_object(MinicParser *parser,
                                             MinicType element_type,
                                             MinicSourceSpan name_span,
                                             char *section_name,
                                             size_t section_capacity,
                                             size_t *section_name_length,
                                             bool *has_section,
                                             size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;
    size_t inferred_count;
    bool infer_bound;

    if (parser == NULL || section_name == NULL || section_name_length == NULL ||
        has_section == NULL || explicit_alignment == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    infer_bound = parser->current.kind == MINIC_TOKEN_RBRACKET;
    element_count = 0U;
    if (infer_bound) {
        if (!minic_parser_advance(parser) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build static scalar array type");
        }
        return false;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create static scalar array object");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (infer_bound) {
            minic_parser_error(parser, "incomplete static array definition requires an initializer");
            return false;
        }
        return minic_c0_global_object_set_zero_initialized(parser->program, object_id) &&
               minic_parser_advance(parser);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        return false;
    }
    if (minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (infer_bound) {
            if (!minic_parser_add_string_literal_initializer(parser, object_id, &inferred_count) ||
                !minic_c0_program_complete_array_type(
                    parser->program, object_type, inferred_count)) {
                minic_parser_error(parser, "cannot infer static character array bound");
                return false;
            }
        } else if (!minic_parser_add_bounded_string_literal_initializer(
                       parser, object_id, element_count)) {
            return false;
        }
    } else if (!parse_static_scalar_array_plan(parser,
                                                object_id,
                                                object_type,
                                                element_type,
                                                element_count,
                                                infer_bound)) {
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static scalar array");
}

'''
p = Path("src/frontend/parser_global.c")
text = p.read_text()
helper_marker = "static bool parse_static_zero_definition(MinicParser *parser,\n"
if text.count(helper_marker) != 1:
    raise SystemExit("parser_global.c: static zero-definition marker not found uniquely")
text = text.replace(helper_marker, static_array_helper + helper_marker, 1)
p.write_text(text)

routing_old = """    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_scalar(parser, element_type, name_span);
    }
    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {
        minic_parser_error(parser, "static global arrays currently require const integer elements");
        return false;
    }
"""
routing_new = """    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_scalar(parser, element_type, name_span);
    }
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        bool single_dimension;

        if (!static_scalar_array_is_single_dimension(parser, &single_dimension)) {
            return false;
        }
        if (single_dimension) {
            return parse_static_scalar_array_object(parser,
                                                    element_type,
                                                    name_span,
                                                    section_name,
                                                    section_capacity,
                                                    section_name_length,
                                                    has_section,
                                                    explicit_alignment);
        }
    }
    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {
        minic_parser_error(parser, "static global arrays currently require const integer elements");
        return false;
    }
"""
replace_once("src/frontend/parser_global.c", routing_old, routing_new)

# Focused regression: designator gaps, GNU range assignment, range overwrite,
# symbolic function/string relocations, writable zero arrays, and inferred mutable arrays.
Path("tests/compiler/c0/static_array_designators.c").write_text(r'''enum Slot {
    SLOT_ONE = 1,
    SLOT_THREE = 3
};

static unsigned long zero_table[4];

static const unsigned long indexed[4] = {
    [SLOT_ONE] = 7UL,
    [SLOT_THREE] = 9UL,
};

static unsigned long ranged[4] = {
    [0 ... 3] = ~0UL,
    [1] = 5UL,
};

static void fallback(void)
{
}

static void real0(void)
{
}

static void real2(void)
{
}

void *const functions[4] = {
    [0 ... 3] = fallback,
    [0] = real0,
    [2] = real2,
};

static const char *const names[] = {
    [0] = "zero",
    [2] = "two",
};

static int mutable_inferred[] = {0, 8, -1, -1, 16};

int main(void)
{
    return (int)(zero_table[0] + indexed[1] + ranged[1] +
                 (functions[0] != 0) + (names[2] != 0) + mutable_inferred[4]);
}
''')

Path("tests/compiler/c0/invalid_static_array_designator_oob.c").write_text(
    "static const int values[2] = { [2] = 1 };\n"
)
Path("tests/compiler/c0/invalid_static_array_designator_range.c").write_text(
    "static const int values[2] = { [1 ... 0] = 1 };\n"
)
Path("tests/compiler/c0/invalid_static_array_designator_nonconstant.c").write_text(
    "static int index_value;\n"
    "static const int values[2] = { [index_value] = 1 };\n"
)

Path("tests/compiler/c0/run-static-array-designators.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-array-designators
asm="$work/static_array_designators.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_array_designators.c" \
    -o "$work/static_array_designators.i"
"$minic" -S "$work/static_array_designators.i" -o "$asm"

grep -F 'zero_table:' "$asm" >/dev/null
grep -F '.size zero_table, 32' "$asm" >/dev/null
grep -F 'indexed:' "$asm" >/dev/null
grep -F '.size indexed, 32' "$asm" >/dev/null
grep -F 'ranged:' "$asm" >/dev/null
grep -F '.size ranged, 32' "$asm" >/dev/null
grep -F 'mutable_inferred:' "$asm" >/dev/null
grep -F '.size mutable_inferred, 20' "$asm" >/dev/null
grep -F 'names:' "$asm" >/dev/null
grep -F '.size names, 24' "$asm" >/dev/null

sed -n '/^functions:/,/^.size functions, 32/p' "$asm" | \
    grep '^  \.dword ' >"$work/functions.actual"
cat >"$work/functions.expected" <<'EOF'
  .dword real0
  .dword fallback
  .dword real2
  .dword fallback
EOF
diff -u "$work/functions.expected" "$work/functions.actual"

sed -n '/^names:/,/^.size names, 24/p' "$asm" | \
    grep '^  \.dword ' >"$work/names.actual"
test "$(wc -l <"$work/names.actual")" -eq 3
test "$(grep -c '^  \.dword \.Lminic_string_' "$work/names.actual")" -eq 2
grep -F '  .dword 0' "$work/names.actual" >/dev/null

expect_failure() {
    name=$1
    message=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null
}

expect_failure invalid_static_array_designator_oob \
    'array designator index is outside the initialized array'
expect_failure invalid_static_array_designator_range \
    'GNU array range designator upper bound is below lower bound'
expect_failure invalid_static_array_designator_nonconstant \
    'array designator requires an integer constant expression'

printf '%s\n' \
    'PASS compiler/c0/static_array_designators c99=1 gnu-range=1 overwrite=1 function-reloc=4 string-reloc=2 inferred=1 writable-zero=1'
''')

# Update broad gate: writable static arrays are now a positive standard-C case,
# and the new designator suite is part of the ordinary source gate.
run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
old_negative = '''expect_compile_failure \\
    invalid_writable_static_global \\
    "static global arrays currently require const integer elements"\n'''
new_positive = '''compile_source writable_static_global invalid_writable_static_global\nprintf '%s\\n' "PASS compiler/c0/writable_static_global"\n\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-static-array-designators.sh"\n'''
if run_text.count(old_negative) != 1:
    raise SystemExit("run.sh: writable static negative anchor not found uniquely")
run_path.write_text(run_text.replace(old_negative, new_positive, 1))

print("staged static scalar array owner convergence and designators")
