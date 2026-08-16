#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S | re.M)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex replacement, found {count}: {pattern[:140]!r}")
    p.write_text(text)


# Shared typed designator syntax/ConstEval owner. Runtime and static paths add
# only their execution/storage-specific constraints above this seam.
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
    raise SystemExit("parser_constant.c: shared designator owner already exists")
p.write_text(text.rstrip() + constant_append + "\n")

# Runtime arrays keep only the forward-only execution restriction locally.
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

# Persisted relocation validation must match the GNU function-designator ->
# void-pointer bridge already approved by parser/sema. Keep one invariant owner
# for insertion and AST verification.
ast_anchor = """bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type);
"""
ast_add = ast_anchor + """bool minic_c0_global_relocation_function_target_compatible(
    const MinicC0Program *program,
    MinicType slot_type,
    MinicFunctionId function_id,
    bool has_explicit_pointer_cast);
"""
replace_once("src/frontend/ast.h", ast_anchor, ast_add)

global_anchor = """bool minic_c0_global_relocation_object_target_compatible(const MinicC0Program *program,
                                                         const MinicGlobalRelocation *relocation,
                                                         MinicType slot_type) {
    MinicType target_type;

    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(
               program, slot_type, target_type, relocation->has_explicit_pointer_cast);
}
"""
global_add = global_anchor + """
bool minic_c0_global_relocation_function_target_compatible(
    const MinicC0Program *program,
    MinicType slot_type,
    MinicFunctionId function_id,
    bool has_explicit_pointer_cast) {
    MinicType slot_pointee;

    return program != NULL && function_id < program->function_count &&
           minic_type_is_pointer(slot_type) && minic_type_pointee(slot_type, &slot_pointee) &&
           (has_explicit_pointer_cast || minic_type_is_function(slot_pointee) ||
            minic_type_is_void(slot_pointee));
}
"""
replace_once("src/frontend/ast_global.c", global_anchor, global_add)
replace_once(
    "src/frontend/ast_global.c",
    """        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION && !minic_type_is_function(slot_pointee) &&
         !has_explicit_pointer_cast) ||
""",
    """        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_c0_global_relocation_function_target_compatible(
             program, slot_type, (MinicFunctionId)target_id, has_explicit_pointer_cast)) ||
""",
)
replace_once(
    "src/frontend/ast_verifier.c",
    """                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     (relocation->target_id >= program->function_count ||
                      (!minic_type_is_function(slot_pointee) &&
                       !relocation->has_explicit_pointer_cast))) ||
""",
    """                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     !minic_c0_global_relocation_function_target_compatible(
                         program,
                         slot_type,
                         (MinicFunctionId)relocation->target_id,
                         relocation->has_explicit_pointer_cast)) ||
""",
)

# Static initializer transaction. Designator overwrite is resolved in local
# slots before the canonical GlobalObject receives final values/relocations.
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
    raise SystemExit("parser_global.c: static pointer initializer anchor not unique")
p.write_text(text.replace(slot_anchor, slot_add, 1))

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

static bool parse_static_scalar_array_transaction(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  bool infer_bound) {
    MinicStaticArraySlot *slots;
    const MinicGlobalObject *object;
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
                minic_parser_error(parser, "too many nested static array initializers");
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
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            !minic_c0_program_complete_array_type(parser->program, object->type, extent)) {
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
    raise SystemExit("parser_global.c: static array constant marker not unique")
p.write_text(text.replace(marker, plan_code + marker, 1))

# Fixed scalar arrays already route here via #250; only their initializer
# representation changes to the transaction when scalar designators are possible.
regex_once(
    "src/frontend/parser_global.c",
    r"static bool parse_static_array_constant\(MinicParser \*parser,\n                                        MinicGlobalObjectId object_id,\n                                        const MinicArrayType \*array_type\) \{.*?\n}\n\nstatic bool parse_static_record_constant",
    r'''static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    MinicType element_type;
    size_t element_count;
    size_t element_index;

    if (array_type == NULL || array_type->element_count == 0U) {
        minic_parser_error(parser, "invalid complete static array initializer type");
        return false;
    }
    element_type = array_type->element_type;
    element_count = array_type->element_count;
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, false);
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

# Inferred integer arrays retain #250's declaration/bound owner but share the
# transaction for positional/designated initializer semantics and bound inference.
regex_once(
    "src/frontend/parser_global.c",
    r"static bool parse_static_inferred_integer_array\(MinicParser \*parser,.*?\n}\n\nstatic bool parse_static_inferred_char_array",
    r'''static bool parse_static_inferred_integer_array(MinicParser *parser,
                                                MinicType element_type,
                                                MinicSourceSpan name_span,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_integer(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static integer array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !parse_static_scalar_array_transaction(
            parser, object_id, element_type, 0U, true)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse inferred static integer array");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after inferred static integer array");
}

static bool parse_static_inferred_char_array''',
)

# Pointer arrays lose their old target-list/zero-only owner and use canonical
# ArrayType + GlobalObject + the same final slot materialization transaction.
regex_once(
    "src/frontend/parser_global.c",
    r"static bool parse_static_pointer_array\(MinicParser \*parser,.*?\n}\n\nstatic bool parse_static_zero_definition",
    r'''static bool parse_static_pointer_array(MinicParser *parser,
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
    bool inferred_bound;

    element_count = 0U;
    inferred_bound = false;
    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_pointer(element_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build static pointer array type");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
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
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment))) {
        minic_parser_error(parser, "cannot create static pointer array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (inferred_bound) {
            minic_parser_error(parser, "incomplete static pointer array requires an initializer");
            return false;
        }
        return minic_c0_global_object_set_zero_initialized(parser->program, object_id) &&
               minic_parser_advance(parser);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static pointer array") ||
        !parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, inferred_bound)) {
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static pointer array");
}

static bool parse_static_zero_definition''',
)

# Focused regression deliberately covers all stacked owners: #250 fixed integer
# arrays, inferred integer arrays, static pointer arrays, and the shared external
# static-storage initializer used by syscall-table-shaped declarations.
Path("tests/compiler/c0/static_array_designators.c").write_text(r'''enum Slot {
    SLOT_ONE = 1,
    SLOT_THREE = 3
};

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

void *const syscall_shape[4] = {
    [0 ... 3] = fallback,
    [0] = real0,
    [2] = real2,
};

static const char *const names[] = {
    [0] = "zero",
    [2] = "two",
};

static int mutable_inferred[] = {
    [3] = 8,
    [1] = 4,
};

int main(void)
{
    return (int)(indexed[1] + ranged[1] + (syscall_shape[0] != 0) +
                 (names[2] != 0) + mutable_inferred[3]);
}
''')
Path("tests/compiler/c0/invalid_static_array_designator_oob.c").write_text(
    "static const int values[2] = { [2] = 1 };\n")
Path("tests/compiler/c0/invalid_static_array_designator_range.c").write_text(
    "static const int values[2] = { [1 ... 0] = 1 };\n")
Path("tests/compiler/c0/invalid_static_array_designator_nonconstant.c").write_text(
    "static int index_value;\nstatic const int values[2] = { [index_value] = 1 };\n")
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

grep -F 'indexed:' "$asm" >/dev/null
grep -F '.size indexed, 32' "$asm" >/dev/null
grep -F 'ranged:' "$asm" >/dev/null
grep -F '.size ranged, 32' "$asm" >/dev/null
grep -F 'mutable_inferred:' "$asm" >/dev/null
grep -F '.size mutable_inferred, 16' "$asm" >/dev/null
grep -F 'names:' "$asm" >/dev/null
grep -F '.size names, 24' "$asm" >/dev/null

sed -n '/^syscall_shape:/,/^.size syscall_shape, 32/p' "$asm" | \
    grep '^  \.dword ' >"$work/syscall.actual"
cat >"$work/syscall.expected" <<'EOF'
  .dword real0
  .dword fallback
  .dword real2
  .dword fallback
EOF
diff -u "$work/syscall.expected" "$work/syscall.actual"

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
    'PASS compiler/c0/static_array_designators c99=1 gnu-range=1 overwrite=1 function-reloc=4 string-reloc=2 inferred-backward=1'
''')

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
anchor = "    run-static-mutable-arrays.sh \\\n"
if run_text.count(anchor) != 1:
    raise SystemExit("run.sh: static mutable runner anchor not found uniquely")
run_path.write_text(run_text.replace(
    anchor,
    anchor + "    run-static-array-designators.sh \\\n",
    1))

print("staged post-250 static designator and relocation convergence")
