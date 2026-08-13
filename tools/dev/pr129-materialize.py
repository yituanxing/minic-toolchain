from pathlib import Path

parser = Path("src/frontend/parser_global.c")
text = parser.read_text()
old = '''static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
    const MinicExpression *expression;

    if (program == NULL || target_object_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression->value.unary.operand);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
        expression->value.global_object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(program, expression->value.global_object_id) == NULL) {
        return false;
    }
    *target_object_id = expression->value.global_object_id;
    return true;
}
'''
new = '''static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicGlobalObjectId object_id;

    if (program == NULL || target_object_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    addressed = minic_c0_program_expression(program, expression->value.unary.operand);
    if (addressed == NULL) {
        return false;
    }
    if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object_id = addressed->value.global_object_id;
    } else if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        const MinicGlobalObject *object;

        base = minic_c0_program_expression(program, addressed->value.subscript.base);
        index = minic_c0_program_expression(program, addressed->value.subscript.index);
        if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT || index == NULL ||
            index->kind != MINIC_EXPRESSION_INTEGER || !minic_type_is_integer(index->type) ||
            index->value.integer_value != 0) {
            return false;
        }
        object_id = base->value.global_object_id;
        object = minic_c0_program_global_object(program, object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return false;
        }
    } else {
        return false;
    }
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(program, object_id) == NULL) {
        return false;
    }
    *target_object_id = object_id;
    return true;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"static relocation resolver anchor mismatch: {text.count(old)}")
parser.write_text(text.replace(old, new, 1))

fixture = Path("tests/compiler/c0/static_object_address_relocation.c")
text = fixture.read_text()
old = '''int external_address_target;
static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);

int read_static_object_addresses(void) {
    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0;
}
'''
new = '''int external_address_target;
int global_address_array[2] = {1, 2};
static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);
static char *string_literal_address = "/init";
static int *array_decay_address = global_address_array;
static int *array_zero_address = &global_address_array[0];

int read_static_object_addresses(void) {
    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0 && string_literal_address != (void *)0 &&
           array_decay_address != (void *)0 && array_zero_address != (void *)0;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"static relocation fixture anchor mismatch: {text.count(old)}")
fixture.write_text(text.replace(old, new, 1))

Path("tests/compiler/c0/invalid_static_pointer_subscript_relocation.c").write_text('''int *runtime_pointer_target;\nstatic int *invalid_pointer_subscript = &runtime_pointer_target[0];\n''')

runner = Path("tests/compiler/c0/run-static-object-address-relocation.sh")
text = runner.read_text()
old = '''count=$(grep -F -c '.dword internal_address_target' "$work/static_object_address_relocation.s")
test "$count" -eq 2

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_type.c" \\
'''
new = '''count=$(grep -F -c '.dword internal_address_target' "$work/static_object_address_relocation.s")
test "$count" -eq 2
array_count=$(grep -F -c '.dword global_address_array' "$work/static_object_address_relocation.s")
test "$array_count" -eq 2
grep -F 'string_literal_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword .Lminic_string_' "$work/static_object_address_relocation.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_type.c" \\
'''
if text.count(old) != 1:
    raise SystemExit(f"static relocation runner positive anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''grep -F 'static pointer initializer requires a null or zero-addend object address constant' \\
    "$work/invalid-addend.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address cast+direct+parenthesized null=shared addend=fail-closed type=checked'
'''
new = '''grep -F 'static pointer initializer requires a null or zero-addend object address constant' \\
    "$work/invalid-addend.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_subscript_relocation.c" \\
    -o "$work/invalid-pointer-subscript.s" \\
    >"$work/invalid-pointer-subscript.stdout" 2>"$work/invalid-pointer-subscript.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/static-object-address: runtime pointer subscript accepted as link-time relocation' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or zero-addend object address constant' \\
    "$work/invalid-pointer-subscript.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address+zero-offset-array-decay+string-literal cast+direct+parenthesized null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'
'''
if text.count(old) != 1:
    raise SystemExit(f"static relocation runner negative anchor mismatch: {text.count(old)}")
runner.write_text(text.replace(old, new, 1))
