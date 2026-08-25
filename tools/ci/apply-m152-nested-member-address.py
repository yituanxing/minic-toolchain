#!/usr/bin/env python3
from pathlib import Path

marker = "M157_SCALAR_COMPOUND_LITERAL_ADDRESS_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M157 scalar compound literal address owner already staged")
    raise SystemExit(0)
if "M156_STRUCTURAL_POINTER_RELATIONAL_OWNER" not in text:
    raise SystemExit("M157 requires productized M156")

anchor = r'''    /* BATCH_U_RECORD_COMPOUND_LITERAL_ADDRESS: a record compound literal is
       an lvalue with a real semantic backing object.  Reuse that object for
       address-of just as the address-backed aggregate seam already does; do
       not synthesize a second temporary and do not special-case call sites. */
'''
if text.count(anchor) != 1:
    raise SystemExit("M157 could not locate compound literal address owner")

insert = r'''    /* M157_SCALAR_COMPOUND_LITERAL_ADDRESS_OWNER: scalar compound literals
       use the same frontend-owned hidden local + initializer block model as
       record compound literals.  Execute that initializer at the expression
       point, reuse the hidden local's Core object, and expose its address so
       the ordinary scalar lvalue-read path performs the final load.  No scalar
       literal value is synthesized separately from its addressable C object. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
        core_memory_scalar_type(expression->type)) {
        const MinicBlock *initializer_block;
        const MinicLocal *local;
        bool terminated;

        local = minic_c0_program_local(
            context->body->program, expression->value.compound_literal.local_id);
        initializer_block = minic_c0_program_block(
            context->body->program, expression->value.compound_literal.initializer_block);
        if (local == NULL || initializer_block == NULL || local->is_array ||
            local->is_register_storage || !minic_type_equal(local->type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        terminated = false;
        status = lower_block(context, initializer_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(
            context, expression->value.compound_literal.local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (object_id >= context->function->object_count ||
            !minic_type_equal(context->function->objects[object_id].type, local->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
text = text.replace(anchor, insert + anchor, 1)
path.write_text(text)

Path("tests/compiler/c0/m157_scalar_compound_literal_address.c").write_text(r'''int scalar_zero(void) {
    return (int){0};
}

int scalar_one(void) {
    return (int){1};
}

int scalar_build_bug_shape(void) {
    return !(!((int){0} != 0));
}

int scalar_reinitialize(int x) {
    return (int){x};
}
''')
print("staged M157 scalar compound literal address owner")
