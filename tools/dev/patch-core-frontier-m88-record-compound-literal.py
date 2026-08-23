#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M88_RECORD_COMPOUND_LITERAL_ADDRESS"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M88 record compound-literal address already applied")
        return 0

    anchor = '''    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return lower_address(context, expression_id, address_id);
    }
'''
    if text.count(anchor) != 1:
        raise SystemExit(f"M88 record-address lvalue anchor count={text.count(anchor)}")

    replacement = '''    /* M88_RECORD_COMPOUND_LITERAL_ADDRESS: a block-scope record compound
       literal already owns a hidden local backing object plus an initializer
       block. Execute that initializer, then expose the backing object's address
       to the existing address-backed record-copy seam. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
        const MinicBlock *initializer_block;
        MinicCoreInstruction address_instruction;
        MinicCoreObjectId object_id;
        MinicCoreLowerStatus status;
        MinicType pointer_type;
        bool terminated;

        initializer_block = minic_c0_program_block(
            context->body->program, expression->value.compound_literal.initializer_block);
        if (initializer_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
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
        if (!minic_type_pointer_to(expression->type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&address_instruction, 0, sizeof(address_instruction));
        address_instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        address_instruction.span = expression->span;
        address_instruction.type = pointer_type;
        address_instruction.result = MINIC_CORE_VALUE_INVALID;
        address_instruction.value.object_id = object_id;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &address_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return lower_address(context, expression_id, address_id);
    }
'''
    PATH.write_text(text.replace(anchor, replacement, 1))
    print("M88 record compound-literal address applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
