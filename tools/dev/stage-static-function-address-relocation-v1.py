#!/usr/bin/env python3
from pathlib import Path

parser_path = Path('src/frontend/parser_global.c')
text = parser_path.read_text()

anchor = '''static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
'''
helper = '''static bool static_function_address_relocation_target(const MinicC0Program *program,
                                                      MinicExpressionId expression_id,
                                                      MinicFunctionId *target_function_id) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicFunctionId function_id;

    if (program == NULL || target_function_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        function_id = expression->value.function_id;
    } else if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        addressed = minic_c0_program_expression(program, expression->value.unary.operand);
        if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_FUNCTION) {
            return false;
        }
        function_id = addressed->value.function_id;
    } else {
        return false;
    }
    if (function_id == MINIC_FUNCTION_INVALID ||
        minic_c0_program_function(program, function_id) == NULL) {
        return false;
    }
    *target_function_id = function_id;
    return true;
}

static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
'''
assert text.count(anchor) == 1
text = text.replace(anchor, helper, 1)

old = '''typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    uint64_t bits;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;
'''
new = '''typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    bool relocation_is_function;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
'''
new = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
'''
new = '''    if (static_function_address_relocation_target(
            parser->program, expression_id, &initializer->function_id)) {
        initializer->has_relocation = true;
        initializer->relocation_is_function = true;
        return true;
    }
    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''        } else {
            MinicExpressionId initializer_id;
            MinicGlobalObjectId target_object_id;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
'''
new = '''        } else {
            MinicExpressionId initializer_id;
            MinicFunctionId target_function_id;
            MinicGlobalObjectId target_object_id;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                    minic_parser_error(parser, "cannot record static null-pointer initializer");
                    return false;
                }
            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
'''
new = '''            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                    minic_parser_error(parser, "cannot record static null-pointer initializer");
                    return false;
                }
            } else if (static_function_address_relocation_target(
                           parser->program, initializer_id, &target_function_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_function_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                        0U,
                        target_function_id)) {
                    minic_parser_error(parser, "cannot record static function-address relocation");
                    return false;
                }
            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''        if (initializer.has_relocation) {
            if (!minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U) ||
                !minic_c0_global_object_add_object_relocation_path(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    slot_index,
                    initializer.relocation_target.object_id,
                    initializer.relocation_target.member_indices,
                    initializer.relocation_target.member_depth)) {
                minic_parser_error(parser, "cannot record nested static object relocation");
                return false;
            }
'''
new = '''        if (initializer.has_relocation) {
            if (!minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {
                minic_parser_error(parser, "cannot reserve nested static relocation slot");
                return false;
            }
            if (initializer.relocation_is_function) {
                if (!minic_c0_global_object_add_function_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                        slot_index,
                        initializer.function_id)) {
                    minic_parser_error(parser, "cannot record nested static function relocation");
                    return false;
                }
            } else if (!minic_c0_global_object_add_object_relocation_path(
                           parser->program,
                           object_id,
                           MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                           slot_index,
                           initializer.relocation_target.object_id,
                           initializer.relocation_target.member_indices,
                           initializer.relocation_target.member_depth)) {
                minic_parser_error(parser, "cannot record nested static object relocation");
                return false;
            }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)

old = '''                       "static pointer initializer requires a null or zero-addend object address "
                       "constant");
'''
new = '''                       "static pointer initializer requires a null or zero-addend symbol address "
                       "constant");
'''
# Only the generic parser diagnostic; scalar fallback has same spelling too and should remain
# aligned with the broader accepted contract, so replace all remaining exact occurrences.
assert text.count(old) >= 1
text = text.replace(old, new)
parser_path.write_text(text)

fixture_path = Path('tests/compiler/c0/static_object_address_relocation.c')
fixture = fixture_path.read_text()
anchor = '''static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
'''
addition = '''static int internal_address_target = 7;
static int function_address_target(int value) { return value + 1; }

struct FunctionAddressHolder {
    void *address;
};

static void *external_address = (void *)&external_address_target;
static void *function_address = (void *)&function_address_target;
static struct FunctionAddressHolder aggregate_function_address = {
    (void *)&function_address_target,
};
'''
assert fixture.count(anchor) == 1
fixture = fixture.replace(anchor, addition, 1)
old = '''    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0 && string_literal_address != (void *)0 &&
           array_decay_address != (void *)0 && array_zero_address != (void *)0;
'''
new = '''    return external_address != (void *)0 && function_address != (void *)0 &&
           aggregate_function_address.address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0 && string_literal_address != (void *)0 &&
           array_decay_address != (void *)0 && array_zero_address != (void *)0;
'''
assert fixture.count(old) == 1
fixture_path.write_text(fixture.replace(old, new, 1))

runner_path = Path('tests/compiler/c0/run-static-object-address-relocation.sh')
runner = runner_path.read_text()
old = '''grep -F 'string_literal_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword .Lminic_string_' "$work/static_object_address_relocation.s" >/dev/null
'''
new = '''grep -F 'string_literal_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword .Lminic_string_' "$work/static_object_address_relocation.s" >/dev/null
function_count=$(grep -F -c '.dword function_address_target' "$work/static_object_address_relocation.s")
test "$function_count" -eq 2
'''
assert runner.count(old) == 1
runner = runner.replace(old, new, 1)
runner = runner.replace(
    "static pointer initializer requires a null or zero-addend object address constant",
    "static pointer initializer requires a null or zero-addend symbol address constant",
)
old = "printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address+zero-offset-array-decay+string-literal cast+direct+parenthesized null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'\n"
new = "printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=object+function scalar+aggregate zero-offset-array-decay+string-literal cast+direct+parenthesized null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'\n"
assert runner.count(old) == 1
runner_path.write_text(runner.replace(old, new, 1))
