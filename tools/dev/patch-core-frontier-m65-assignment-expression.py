from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE'
if marker in text:
    print('M65 scalar assignment expression value already applied')
    raise SystemExit(0)

anchor = '''    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
'''
replacement = '''    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* M65_SCALAR_ASSIGNMENT_EXPRESSION_VALUE: simple assignment is an
       expression as well as a side effect. Preserve the assigned scalar across
       evaluation of the destination address, perform the store, then yield the
       stored (unqualified) value. This is the same semantic seam used by an
       assignment statement, without inventing a Linux/bitmap special case. */
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreInstruction store;
        MinicCoreObjectId stored_object;
        MinicCoreValueId address;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType expression_value_type;
        MinicType stored_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL ||
            target->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type) ||
            !minic_type_unqualified(expression->type, &expression_value_type) ||
            !minic_type_equal(expression_value_type, stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(
            context, stored_type, expression->value.binary.right, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = spill_scalar_value(
            context, expression->span, stored_type, stored_value, &stored_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_address(context, expression->value.binary.left, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, expression->span, stored_type, stored_object, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&store, 0, sizeof(store));
        store.kind = MINIC_CORE_INSTRUCTION_STORE;
        store.span = expression->span;
        store.type = minic_type_void();
        store.result = MINIC_CORE_VALUE_INVALID;
        store.value.store.address = address;
        store.value.store.stored_value = stored_value;
        store.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &store)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_DISCARD) {
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M65 anchor count={text.count(anchor)}')
path.write_text(text.replace(anchor, replacement, 1))
print('M65 scalar assignment expression value applied')
