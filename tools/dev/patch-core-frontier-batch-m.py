#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: anchor count={count}")
    return text.replace(old, new, 1)


# --- Core IR model ---------------------------------------------------------
path = Path("src/core/core_ir.h")
text = path.read_text()
text = replace_once(
    text,
    '''    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    /* M80_ADDRESS_BACKED_RECORD_COPY: byte-preserving aggregate memory copy. */\n    MINIC_CORE_INSTRUCTION_RECORD_COPY,\n''',
    '''    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    /* BATCH_M_RECORD_LOAD: materialize one address-backed aggregate value into\n       a private Core object while preserving source volatility. */\n    MINIC_CORE_INSTRUCTION_RECORD_LOAD,\n    /* M80_ADDRESS_BACKED_RECORD_COPY: byte-preserving aggregate memory copy. */\n    MINIC_CORE_INSTRUCTION_RECORD_COPY,\n''',
    "core_ir.h record-load kind",
)
text = replace_once(
    text,
    '''        struct {\n            MinicCoreValueId address;\n            MinicCoreValueId stored_value;\n            bool is_volatile;\n        } store;\n        struct {\n            MinicCoreValueId destination_address;\n            MinicCoreValueId source_address;\n        } record_copy;\n''',
    '''        struct {\n            MinicCoreValueId address;\n            MinicCoreValueId stored_value;\n            bool is_volatile;\n        } store;\n        struct {\n            MinicCoreValueId source_address;\n            MinicCoreObjectId destination_object;\n            bool is_volatile;\n        } record_load;\n        struct {\n            MinicCoreValueId destination_address;\n            MinicCoreValueId source_address;\n        } record_copy;\n''',
    "core_ir.h record-load payload",
)
path.write_text(text)

# --- Core verifier + dump --------------------------------------------------
path = Path("src/core/core_ir.c")
text = path.read_text()
record_load_verify = r'''    /* BATCH_M_RECORD_LOAD: source qualification is semantic metadata.  The
       destination is an unqualified private snapshot object. */
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        MinicCoreObjectId destination_object;
        MinicType record_type;
        MinicType source_pointee;
        MinicType source_type;

        destination_object = instruction->value.record_load.destination_object;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_record(instruction->type) &&
               minic_type_unqualified(instruction->type, &record_type) &&
               minic_type_equal(record_type, instruction->type) &&
               destination_object < function->object_count &&
               minic_type_equal(function->objects[destination_object].type,
                                instruction->type) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_load.source_address,
                                         &source_pointee) &&
               minic_type_unqualified(source_pointee, &source_type) &&
               minic_type_equal(source_type, instruction->type) &&
               instruction->value.record_load.is_volatile ==
                   minic_type_is_volatile(source_pointee);
    }
'''
anchor = '''    /* M80_ADDRESS_BACKED_RECORD_COPY: both SSA operands are addresses to the\n       same unqualified record type; legality of writing a const-qualified\n       destination is already established by the frontend initializer/copy node. */\n    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {\n'''
text = replace_once(text, anchor, record_load_verify + anchor, "core_ir.c record-load verifier")
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return fprintf(output,\n                       "  record.copy %%%" PRIu32 ", %%%" PRIu32 "\\n",\n                       instruction->value.record_copy.source_address,\n                       instruction->value.record_copy.destination_address) >= 0;\n''',
    '''    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:\n        return fprintf(output,\n                       "  record.load%s %%%" PRIu32 ", %%o%" PRIu32 "\\n",\n                       instruction->value.record_load.is_volatile ? ".volatile" : "",\n                       instruction->value.record_load.source_address,\n                       instruction->value.record_load.destination_object) >= 0;\n    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return fprintf(output,\n                       "  record.copy %%%" PRIu32 ", %%%" PRIu32 "\\n",\n                       instruction->value.record_copy.source_address,\n                       instruction->value.record_copy.destination_address) >= 0;\n''',
    "core_ir.c record-load dump",
)
path.write_text(text)

# --- Core lowering ---------------------------------------------------------
path = Path("src/core/core_lower.c")
text = path.read_text()
helper = r'''/* BATCH_M_RECORD_LOAD: turn an address-backed record rvalue/lvalue wrapper
   into a private Core snapshot object.  The source pointer keeps its qualifiers
   so volatile aggregate reads remain explicit at the IR boundary. */
static MinicCoreLowerStatus lower_record_load_object(MinicCoreLowerContext *context,
                                                     MinicExpressionId expression_id,
                                                     MinicCoreObjectId *object_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_address;
    MinicType expression_type;
    MinicType source_pointee;
    MinicType source_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || object_id == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id) ||
        !minic_type_unqualified(expression->type, &expression_type) ||
        !minic_type_is_record(expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_record_value_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_address >= context->function->value_count ||
        !minic_type_pointee(context->function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, expression_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (!minic_core_function_add_object(
            context->function, expression->span, expression_type, object_id)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    instruction.span = expression->span;
    instruction.type = expression_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_load.source_address = source_address;
    instruction.value.record_load.destination_object = *object_id;
    instruction.value.record_load.is_volatile = minic_type_is_volatile(source_pointee);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
anchor = '''static MinicCoreLowerStatus lower_record_copy_statement(MinicCoreLowerContext *context,\n                                                        const MinicStatement *statement) {\n'''
text = replace_once(text, anchor, helper + anchor, "core_lower.c record-load helper")
text = replace_once(
    text,
    '''            } else if (expression->kind == MINIC_EXPRESSION_CALL &&\n                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {\n                status = lower_direct_record_call_object(\n                    context, expression, &terminator.return_object);\n            } else {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n''',
    '''            } else if (expression->kind == MINIC_EXPRESSION_CALL &&\n                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {\n                status = lower_direct_record_call_object(\n                    context, expression, &terminator.return_object);\n            } else if (minic_c0_record_value_is_address_backed(\n                           context->body->program, statement->expression)) {\n                status = lower_record_load_object(\n                    context, statement->expression, &terminator.return_object);\n            } else {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n''',
    "core_lower.c address-backed record return",
)
path.write_text(text)

# --- RV64 support + emission ----------------------------------------------
path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()
support = r'''static bool core_record_load_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       size_t *record_size) {
    MinicCoreObjectId destination_object;
    MinicCoreValueId source_address;
    MinicType record_type;
    MinicType source_pointee;
    MinicType source_type;
    size_t alignment;
    size_t size;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_record(instruction->type) ||
        !minic_type_unqualified(instruction->type, &record_type) ||
        !minic_type_equal(record_type, instruction->type)) {
        return false;
    }
    destination_object = instruction->value.record_load.destination_object;
    source_address = instruction->value.record_load.source_address;
    if (destination_object >= function->object_count || source_address >= function->value_count ||
        !minic_type_equal(function->objects[destination_object].type, instruction->type) ||
        !minic_type_pointee(function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, instruction->type) ||
        instruction->value.record_load.is_volatile != minic_type_is_volatile(source_pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment) ||
        (size != 1U && size != 2U && size != 4U && size != 8U)) {
        return false;
    }
    (void)alignment;
    if (record_size != NULL) {
        *record_size = size;
    }
    return true;
}

'''
anchor = '''static bool core_record_copy_supported(const MinicC0Program *program,\n                                       const MinicCoreFunction *function,\n                                       const MinicCoreInstruction *instruction) {\n'''
text = replace_once(text, anchor, support + anchor, "core_codegen.c record-load support helper")
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return core_record_copy_supported(program, function, instruction);\n''',
    '''    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:\n        return core_record_load_supported(program, function, instruction, NULL);\n    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return core_record_copy_supported(program, function, instruction);\n''',
    "core_codegen.c record-load support switch",
)
emit = r'''    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!core_record_load_supported(program, function, instruction, &record_size) ||
            !core_object_offset(program,
                                function,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !load_core_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                 record_size == 2U ? "lhu" : "lbu";
        if (fprintf(file, "  %s t1, 0(t0)\n", opcode) < 0 ||
            !emit_sp_store_chunk(file, "t1", destination_offset, record_size)) {
            return false;
        }
        return true;
    }
'''
anchor = '''    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {\n        size_t alignment;\n'''
text = replace_once(text, anchor, emit + anchor, "core_codegen.c record-load emitter")
path.write_text(text)

print("CORE_BATCH_M_PATCHED volatile address-backed record return load")
