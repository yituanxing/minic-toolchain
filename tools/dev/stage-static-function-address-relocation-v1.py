#!/usr/bin/env python3
from pathlib import Path

# AST: persist the semantic fact that a symbolic address passed through an explicit pointer cast.
ast_path = Path('src/frontend/ast.h')
ast = ast_path.read_text()
old = '''typedef struct MinicGlobalRelocation {
    MinicGlobalRelocationLocationKind location_kind;
    size_t location_index;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t target_member_depth;
} MinicGlobalRelocation;
'''
new = '''typedef struct MinicGlobalRelocation {
    MinicGlobalRelocationLocationKind location_kind;
    size_t location_index;
    MinicGlobalRelocationTargetKind target_kind;
    size_t target_id;
    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t target_member_depth;
    bool has_explicit_pointer_cast;
} MinicGlobalRelocation;
'''
assert ast.count(old) == 1
ast = ast.replace(old, new, 1)
old = '''bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    MinicGlobalRelocationLocationKind location_kind,
                                                    size_t location_index,
                                                    MinicFunctionId function_id);
'''
new = '''bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,
                                                    MinicGlobalObjectId global_object_id,
                                                    MinicGlobalRelocationLocationKind location_kind,
                                                    size_t location_index,
                                                    MinicFunctionId function_id);
bool minic_c0_global_object_add_function_relocation_cast(MinicC0Program *program,
                                                         MinicGlobalObjectId global_object_id,
                                                         MinicGlobalRelocationLocationKind location_kind,
                                                         size_t location_index,
                                                         MinicFunctionId function_id);
'''
assert ast.count(old) == 1
ast = ast.replace(old, new, 1)
old = '''bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth);
'''
new = '''bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth);
bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth);
'''
assert ast.count(old) == 1
ast_path.write_text(ast.replace(old, new, 1))

# Relocation owner: direct compatibility stays strict; explicit pointer casts are an explicit semantic path.
global_path = Path('src/frontend/ast_global.c')
global_text = global_path.read_text()
old = '''static bool global_relocation_object_target_type_compatible(const MinicC0Program *program,
                                                            MinicType slot_type,
                                                            MinicType target_type) {
'''
new = '''static bool global_relocation_object_target_type_compatible(const MinicC0Program *program,
                                                            MinicType slot_type,
                                                            MinicType target_type,
                                                            bool has_explicit_pointer_cast) {
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''    if (program == NULL || !minic_type_is_pointer(slot_type)) {
        return false;
    }
'''
new = '''    if (program == NULL || !minic_type_is_pointer(slot_type)) {
        return false;
    }
    if (has_explicit_pointer_cast) {
        /* The parser has already validated the explicit pointer-to-pointer cast.
         * Re-check only the normalized type-level legality here; target identity
         * and member-path validity are still verified independently. */
        if (minic_type_pointer_to(target_type, &source_pointer_type) &&
            minic_type_cast_compatible(slot_type, source_pointer_type)) {
            return true;
        }
        if (minic_type_is_array(target_type)) {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, target_type.array_type_id);
            if (array_type != NULL &&
                minic_type_pointer_to(array_type->element_type, &source_pointer_type) &&
                minic_type_cast_compatible(slot_type, source_pointer_type)) {
                return true;
            }
        }
    }
'''
# This exact guard appears once in this helper.
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(program, slot_type, target_type);
'''
new = '''    return minic_c0_global_relocation_object_target_type(program, relocation, &target_type) &&
           global_relocation_object_target_type_compatible(
               program, slot_type, target_type, relocation->has_explicit_pointer_cast);
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''static bool add_global_symbol_relocation(MinicC0Program *program,
                                         MinicGlobalObjectId global_object_id,
                                         MinicGlobalRelocationLocationKind location_kind,
                                         size_t location_index,
                                         MinicGlobalRelocationTargetKind target_kind,
                                         size_t target_id,
                                         const size_t *target_member_indices,
                                         size_t target_member_depth) {
'''
new = '''static bool add_global_symbol_relocation(MinicC0Program *program,
                                         MinicGlobalObjectId global_object_id,
                                         MinicGlobalRelocationLocationKind location_kind,
                                         size_t location_index,
                                         MinicGlobalRelocationTargetKind target_kind,
                                         size_t target_id,
                                         const size_t *target_member_indices,
                                         size_t target_member_depth,
                                         bool has_explicit_pointer_cast) {
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''        !minic_type_pointee(slot_type, &slot_pointee) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_type_is_function(slot_pointee)) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee)) ||
'''
new = '''        !minic_type_pointee(slot_type, &slot_pointee) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         !minic_type_is_function(slot_pointee) && !has_explicit_pointer_cast) ||
        (target_kind == MINIC_GLOBAL_RELOCATION_OBJECT && minic_type_is_function(slot_pointee) &&
         !has_explicit_pointer_cast) ||
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''          !global_relocation_object_target_type_compatible(program, slot_type, target_type))) ||
'''
new = '''          !global_relocation_object_target_type_compatible(
              program, slot_type, target_type, has_explicit_pointer_cast))) ||
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''    relocation->target_member_depth = target_member_depth;
'''
new = '''    relocation->target_member_depth = target_member_depth;
    relocation->has_explicit_pointer_cast = has_explicit_pointer_cast;
'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
# Existing direct APIs stay strict.
old = '''                                        function_id,
                                        NULL,
                                        0U);
}

bool minic_c0_global_object_set_extern'''
new = '''                                        function_id,
                                        NULL,
                                        0U,
                                        false);
}

bool minic_c0_global_object_add_function_relocation_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicFunctionId function_id) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_FUNCTION,
                                        function_id,
                                        NULL,
                                        0U,
                                        true);
}

bool minic_c0_global_object_set_extern'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
old = '''                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth);
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,'''
new = '''                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        false);
}

bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        true);
}

bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,'''
assert global_text.count(old) == 1
global_text = global_text.replace(old, new, 1)
# Object wrapper still calls strict path; no signature change needed.
global_path.write_text(global_text)

# Verifier mirrors the same semantic invariant.
verify_path = Path('src/frontend/ast_verifier.c')
verify = verify_path.read_text()
old = '''                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                     (relocation->target_id >= program->global_object_count ||
                      minic_type_is_function(slot_pointee))) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     (relocation->target_id >= program->function_count ||
                      !minic_type_is_function(slot_pointee))) ||
'''
new = '''                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                     (relocation->target_id >= program->global_object_count ||
                      (minic_type_is_function(slot_pointee) &&
                       !relocation->has_explicit_pointer_cast))) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     (relocation->target_id >= program->function_count ||
                      (!minic_type_is_function(slot_pointee) &&
                       !relocation->has_explicit_pointer_cast))) ||
'''
assert verify.count(old) == 1
verify = verify.replace(old, new, 1)
verify_path.write_text(verify)

# Parser: recognize function symbol addresses and retain whether the normalized expression had an explicit pointer cast.
parser_path = Path('src/frontend/parser_global.c')
text = parser_path.read_text()
anchor = '''static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
'''
helper = '''static bool static_pointer_expression_has_explicit_cast(const MinicC0Program *program,
                                                        MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type) &&
            operand != NULL && minic_type_is_pointer(operand->type)) {
            return true;
        }
        expression = operand;
    }
    return false;
}

static bool static_function_address_relocation_target(const MinicC0Program *program,
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
        expression = minic_c0_program_expression(program, expression->value.unary.operand);
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
    bool has_explicit_pointer_cast;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
'''
new = '''    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    initializer->has_explicit_pointer_cast =
        static_pointer_expression_has_explicit_cast(parser->program, expression_id);
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
'''
new = '''    if (static_function_address_relocation_target(
            parser->program, expression_id, &initializer->function_id)) {
        initializer->has_relocation = true;
        initializer->relocation_is_function = true;
        return true;
    }
    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
# Scalar generic pointer path.
old = '''        } else {
            MinicExpressionId initializer_id;
            MinicGlobalObjectId target_object_id;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
'''
new = '''        } else {
            MinicExpressionId initializer_id;
            MinicFunctionId target_function_id;
            MinicGlobalObjectId target_object_id;
            bool has_explicit_pointer_cast;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
'''
new = '''            has_explicit_pointer_cast =
                static_pointer_expression_has_explicit_cast(parser->program, initializer_id);
            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
'''
# Only scalar occurrence before target branches.
assert text.count(old) >= 1
text = text.replace(old, new, 1)
old = '''            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
'''
new = '''            } else if (static_function_address_relocation_target(
                           parser->program, initializer_id, &target_function_id)) {
                const bool recorded = has_explicit_pointer_cast
                                          ? minic_c0_global_object_add_function_relocation_cast(
                                                parser->program,
                                                object_id,
                                                MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                0U,
                                                target_function_id)
                                          : minic_c0_global_object_add_function_relocation(
                                                parser->program,
                                                object_id,
                                                MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                0U,
                                                target_function_id);

                if (!recorded ||
                    !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                    minic_parser_error(parser, "cannot record static function-address relocation");
                    return false;
                }
            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
old = '''                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_object_relocation(parser->program,
                                                                  object_id,
                                                                  MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                                  0U,
                                                                  target_object_id)) {
'''
new = '''                const bool recorded = has_explicit_pointer_cast
                                          ? minic_c0_global_object_add_object_relocation_path_cast(
                                                parser->program,
                                                object_id,
                                                MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                0U,
                                                target_object_id,
                                                NULL,
                                                0U)
                                          : minic_c0_global_object_add_object_relocation(
                                                parser->program,
                                                object_id,
                                                MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                0U,
                                                target_object_id);

                if (!recorded ||
                    !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
# Aggregate persistence.
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
            bool recorded;

            if (!minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {
                minic_parser_error(parser, "cannot reserve nested static relocation slot");
                return false;
            }
            if (initializer.relocation_is_function) {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_function_relocation_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.function_id)
                               : minic_c0_global_object_add_function_relocation(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.function_id);
            } else {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_object_relocation_path_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth)
                               : minic_c0_global_object_add_object_relocation_path(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth);
            }
            if (!recorded) {
                minic_parser_error(parser, "cannot record nested static symbolic relocation");
                return false;
            }
'''
assert text.count(old) == 1
text = text.replace(old, new, 1)
text = text.replace(
    'static pointer initializer requires a null or zero-addend object address ',
    'static pointer initializer requires a null or zero-addend symbol address ',
)
parser_path.write_text(text)

# Regression: explicit object pointer cast + function address cast, scalar and aggregate.
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
static char *explicit_object_cast_address = (char *)&external_address_target;
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
new = '''    return external_address != (void *)0 && explicit_object_cast_address != (void *)0 &&
           function_address != (void *)0 && aggregate_function_address.address != (void *)0 &&
           internal_address != (void *)0 && parenthesized_address != (void *)0 &&
           string_literal_address != (void *)0 && array_decay_address != (void *)0 &&
           array_zero_address != (void *)0;
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
external_count=$(grep -F -c '.dword external_address_target' "$work/static_object_address_relocation.s")
test "$external_count" -eq 2
'''
assert runner.count(old) == 1
runner = runner.replace(old, new, 1)
runner = runner.replace(
    'static pointer initializer requires a null or zero-addend object address constant',
    'static pointer initializer requires a null or zero-addend symbol address constant',
)
old = "printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address+zero-offset-array-decay+string-literal cast+direct+parenthesized null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'\n"
new = "printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=symbolic-object+function explicit-pointer-cast=preserved scalar+aggregate=1 zero-offset-array-decay+string-literal null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'\n"
assert runner.count(old) == 1
runner_path.write_text(runner.replace(old, new, 1))
