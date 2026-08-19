#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


def sub_once(path, pattern, replacement, flags=0):
    text = read(path)
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex occurrence, found {count}: {pattern}")
    write(path, new_text)


# Build the new target-neutral initializer semantic owner.
replace_once(
    "Makefile",
    "\tsrc/frontend/function_body.c \\\n",
    "\tsrc/frontend/function_body.c \\\n\tsrc/frontend/initializer.c \\\n",
)

# Static scalar arrays: syntax parsing produces source-order actions; final storage slots are a
# separate lowering step. This removes last-wins/extent ownership from parser_global.c.
replace_once(
    "src/frontend/parser_global.c",
    '#include "frontend/parser_internal.h"\n',
    '#include "frontend/parser_internal.h"\n#include "frontend/initializer.h"\n',
)

static_function = r"static bool parse_static_scalar_array_transaction\(MinicParser \*parser,.*?\n\}\n\nstatic bool parse_static_forward_array_initializer"
static_replacement = r'''static bool parse_static_scalar_array_transaction(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  bool infer_bound) {
    MinicArrayInitializerPlan plan;
    MinicStaticArraySlot *action_values;
    MinicStaticArraySlot *final_slots;
    const MinicGlobalObject *object;
    size_t action_capacity;
    size_t final_capacity;
    size_t extent;
    size_t index;
    bool success;

    action_values = NULL;
    final_slots = NULL;
    action_capacity = 0U;
    final_capacity = 0U;
    extent = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static scalar array initializer");
        }
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static array designator extent overflows");
                }
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many nested static array initializers");
            goto done;
        }
        if (!grow_static_array_slots(
                parser, &action_values, &action_capacity, action_id + 1U) ||
            !parse_static_array_scalar_slot(parser, element_type, &action_values[action_id])) {
            goto done;
        }
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
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        goto done;
    }
    extent = minic_array_initializer_plan_element_count(&plan);
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
    if (!grow_static_array_slots(parser, &final_slots, &final_capacity, extent)) {
        goto done;
    }
    for (index = 0U; index < extent; ++index) {
        size_t owner;

        if (!minic_array_initializer_plan_final_owner(&plan, index, &owner)) {
            minic_parser_error(parser, "cannot resolve static array initializer owner");
            goto done;
        }
        if (owner != MINIC_INITIALIZER_ACTION_INVALID) {
            final_slots[index] = action_values[owner];
        }
    }
    if (!materialize_static_array_slots(parser, object_id, element_type, final_slots, extent)) {
        goto done;
    }
    success = true;

done:
    free(action_values);
    free(final_slots);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool parse_static_forward_array_initializer'''
sub_once("src/frontend/parser_global.c", static_function, static_replacement, re.S)

# Runtime scalar arrays: parse the whole initializer into the same selection/override plan, then
# lower surviving actions. A range action with multiple surviving elements gets one hidden scalar
# local so its RHS is evaluated exactly once.
replace_once(
    "src/frontend/parser_statement.c",
    '#include "frontend/parser_internal.h"\n',
    '#include "frontend/parser_internal.h"\n#include "frontend/initializer.h"\n',
)

# Keep the existing aggregate-record implementation as the legacy consumer for this first array
# slice. Scalar arrays are fully moved to the semantic plan below.
replace_once(
    "src/frontend/parser_statement.c",
    "static bool parse_fixed_runtime_array_initializer(MinicParser *parser,",
    "static bool parse_fixed_runtime_record_array_initializer_legacy(MinicParser *parser,",
)

runtime_insert_anchor = "\nstatic bool\nparse_local_array_initializer(MinicParser *parser, MinicLocalId local_id, bool infer_count) {"
runtime_helpers = r'''

static bool grow_runtime_array_action_values(MinicParser *parser,
                                             MinicExpressionId **values,
                                             size_t *capacity,
                                             size_t required) {
    MinicExpressionId *resized;
    size_t old_capacity;
    size_t new_capacity;
    size_t index;

    if (parser == NULL || values == NULL || capacity == NULL) {
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
    if (new_capacity < required || new_capacity > SIZE_MAX / sizeof(**values)) {
        minic_parser_error(parser, "runtime array initializer action count overflows");
        return false;
    }
    resized = (MinicExpressionId *)realloc(*values, new_capacity * sizeof(**values));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while planning runtime array initializer");
        return false;
    }
    for (index = old_capacity; index < new_capacity; ++index) {
        resized[index] = MINIC_EXPRESSION_INVALID;
    }
    *values = resized;
    *capacity = new_capacity;
    return true;
}

static bool add_runtime_initializer_once_read(MinicParser *parser,
                                              MinicType value_type,
                                              MinicExpressionId value_id,
                                              MinicExpressionId *read_id) {
    const MinicExpression *value;
    MinicExpression lvalue_read;
    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicStatement assignment;
    MinicType temporary_type;

    if (parser == NULL || read_id == NULL ||
        !minic_type_unqualified(value_type, &temporary_type) ||
        !apply_assignment_conversion(parser, temporary_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, temporary_type, value_id)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "runtime range initializer value type mismatch");
        }
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL) {
        return false;
    }
    (void)memset(&local, 0, sizeof(local));
    local.name_span = value->span;
    local.type = temporary_type;
    local.element_count = 1U;
    local.is_array = false;
    local.is_register_storage = false;
    if (!minic_c0_program_add_local(parser->program, &local, &local_id) ||
        !add_local_lvalue_expression(parser, local_id, value->span, &target_id)) {
        minic_parser_error(parser, "cannot create evaluate-once initializer temporary");
        return false;
    }

    (void)memset(&assignment, 0, sizeof(assignment));
    assignment.kind = MINIC_STATEMENT_ASSIGN;
    assignment.span = value->span;
    assignment.target_expression = target_id;
    assignment.expression = value_id;
    assignment.target_statement = MINIC_STATEMENT_INVALID;
    assignment.cleanup_context = parser->cleanup_context;
    assignment.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    assignment.then_block = MINIC_BLOCK_INVALID;
    assignment.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &assignment)) {
        return false;
    }

    (void)memset(&lvalue_read, 0, sizeof(lvalue_read));
    lvalue_read.kind = MINIC_EXPRESSION_LVALUE_READ;
    lvalue_read.span = value->span;
    lvalue_read.type = temporary_type;
    lvalue_read.value_category = MINIC_VALUE_RVALUE;
    lvalue_read.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &lvalue_read, read_id);
}

static bool lower_runtime_scalar_array_plan(MinicParser *parser,
                                            MinicExpressionId base_id,
                                            MinicType element_type,
                                            size_t element_count,
                                            MinicSourceSpan initializer_span,
                                            const MinicArrayInitializerPlan *plan,
                                            const MinicExpressionId *action_values) {
    size_t action_id;
    size_t index;

    for (index = 0U; index < element_count; ++index) {
        size_t owner;

        if (!minic_array_initializer_plan_final_owner(plan, index, &owner)) {
            return false;
        }
        if (owner == MINIC_INITIALIZER_ACTION_INVALID &&
            !add_array_object_zero_element(parser, base_id, index, initializer_span)) {
            return false;
        }
    }

    for (action_id = 0U; action_id < plan->action_count; ++action_id) {
        size_t final_count;
        MinicExpressionId lowered_value_id;

        final_count = minic_array_initializer_plan_action_final_count(plan, action_id);
        if (final_count == 0U) {
            /* A fully-overridden initializer has unspecified side effects; GCC discards it. */
            continue;
        }
        lowered_value_id = action_values[action_id];
        if (lowered_value_id == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        if (final_count > 1U &&
            !add_runtime_initializer_once_read(
                parser, element_type, lowered_value_id, &lowered_value_id)) {
            return false;
        }
        for (index = 0U; index < element_count; ++index) {
            if (minic_array_initializer_plan_action_owns(plan, action_id, index) &&
                !add_array_object_element_assignment(parser, base_id, index, lowered_value_id)) {
                return false;
            }
        }
    }
    return true;
}

static bool parse_fixed_runtime_scalar_array_initializer(MinicParser *parser,
                                                         MinicExpressionId base_id,
                                                         MinicType element_type,
                                                         size_t element_count) {
    MinicArrayInitializerPlan plan;
    MinicExpressionId *action_values;
    MinicSourceSpan initializer_span;
    size_t action_capacity;
    bool success;

    action_values = NULL;
    action_capacity = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, false);
    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE ||
        minic_type_is_record(element_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid runtime scalar array initializer");
        }
        goto done;
    }
    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;
        size_t action_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(
                    parser, element_count, false, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many runtime array initializer elements");
            goto done;
        }
        if (!grow_runtime_array_action_values(
                parser, &action_values, &action_capacity, action_id + 1U) ||
            !minic_parser_parse_expression(parser, &value_id, 0U)) {
            goto done;
        }
        action_values[action_id] = value_id;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            goto done;
        }
    }
    initializer_span.end = parser->current.span.end;
    if (!minic_parser_advance(parser) ||
        !lower_runtime_scalar_array_plan(parser,
                                         base_id,
                                         element_type,
                                         element_count,
                                         initializer_span,
                                         &plan,
                                         action_values)) {
        goto done;
    }
    success = true;

done:
    free(action_values);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                  MinicExpressionId base_id,
                                                  size_t element_count) {
    const MinicExpression *base;
    MinicArrayObjectInfo array_info;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL ||
        !minic_c0_expression_array_object_info(parser->program, base, &array_info)) {
        return false;
    }
    if (minic_type_is_record(array_info.element_type)) {
        return parse_fixed_runtime_record_array_initializer_legacy(parser, base_id, element_count);
    }
    return parse_fixed_runtime_scalar_array_initializer(
        parser, base_id, array_info.element_type, element_count);
}
'''
replace_once(
    "src/frontend/parser_statement.c",
    runtime_insert_anchor,
    runtime_helpers + runtime_insert_anchor,
)

# The semantic gate now treats arbitrary scalar range RHS and backward designators as supported.
script_path = "tests/compiler/c0/run-gnu-array-range-initializer.sh"
script = read(script_path)
old_nonconstant = '''if "$minic" -S "$work/nonconstant-range.i" -o "$work/nonconstant-range.s" \\
  >"$work/nonconstant-range.stdout" 2>"$work/nonconstant-range.stderr"; then
  printf '%s\\n' 'FAIL compiler/c0/gnu-array-range-initializer: nonconstant range value accepted' >&2
  exit 1
fi
grep -F 'multi-element runtime array range initializer requires an integer constant value' \\
  "$work/nonconstant-range.stderr" >/dev/null
'''
new_nonconstant = '''"$minic" -S "$work/nonconstant-range.i" -o "$work/nonconstant-range.s"
test -s "$work/nonconstant-range.s"
'''
if old_nonconstant not in script:
    raise SystemExit("range gate: nonconstant negative block not found")
script = script.replace(old_nonconstant, new_nonconstant, 1)
old_backward = '''if "$minic" -S "$work/backward.i" -o "$work/backward.s" \\
  >"$work/backward.stdout" 2>"$work/backward.stderr"; then
  printf '%s\\n' 'FAIL compiler/c0/gnu-array-range-initializer: backward designator accepted' >&2
  exit 1
fi
grep -F 'backward runtime array designators are not supported yet' \\
  "$work/backward.stderr" >/dev/null
'''
new_backward = '''"$minic" -S "$work/backward.i" -o "$work/backward.s"
test -s "$work/backward.s"
'''
if old_backward not in script:
    raise SystemExit("range gate: backward negative block not found")
script = script.replace(old_backward, new_backward, 1)
script = script.replace(
    "multi-range-constant=1 single-range-runtime=1 nonconstant-index=1 forward-only=1 bounds=checked",
    "multi-range-evaluate-once=1 single-range-runtime=1 nonconstant-index=1 backward=1 override=last-wins bounds=checked",
    1,
)
write(script_path, script)

# Real runtime differential for evaluate-once + partial override + backward designator.
write(
    "tests/compiler/c0/gnu_array_range_runtime.c",
    '''static int calls;

static int once(int value)
{
    calls += 1;
    return value;
}

int main(void)
{
    int ranged[4] = { [0 ... 3] = once(7), [2] = 9 };
    int backward[3] = { [1] = 3, [0] = 4 };

    return calls == 1 && ranged[0] == 7 && ranged[1] == 7 &&
                   ranged[2] == 9 && ranged[3] == 7 && backward[0] == 4 &&
                   backward[1] == 3 && backward[2] == 0
               ? 0
               : 1;
}
''',
)
replace_once(
    "tests/compiler/c0/run-runtime.sh",
    "run_case array_declaration 0 array_declaration\n",
    "run_case array_declaration 0 array_declaration\nrun_case gnu_array_range_runtime 0 gnu_array_range_runtime\n",
)

print("INITIALIZER_ARRAY_PLAN_V1_MATERIALIZED")
