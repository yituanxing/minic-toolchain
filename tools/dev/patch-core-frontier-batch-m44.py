#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M44 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Core IR: represent the address of a basic block as a first-class pointer value.
replace_once(
    "src/core/core_ir.h",
    '''    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n''',
    '''    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n''',
    "block-address instruction kind",
)
replace_once(
    "src/core/core_ir.h",
    '''        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        struct {\n''',
    '''        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        MinicCoreBlockId block_id;\n        struct {\n''',
    "block-address payload",
)

replace_once(
    "src/core/core_ir.c",
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.global_id >= function->global_count ||\n            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {\n''',
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n\n        if (!instruction_result_is_valid(function, instruction) ||\n            instruction->value.global_id >= function->global_count ||\n            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,\n                                   &pointer_type)) {\n            return false;\n        }\n        return minic_type_equal(pointer_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {\n        MinicType pointee;\n\n        return instruction_result_is_valid(function, instruction) &&\n               instruction->value.block_id < function->block_count &&\n               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);\n    }\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {\n''',
    "block-address verifier",
)
replace_once(
    "src/core/core_ir.c",
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n            return false;\n        }\n        return fprintf(output,\n                       "  %%%" PRIu32 " = global.addr @%s\\n",\n                       instruction->result,\n                       function->globals[instruction->value.global_id].name) >= 0;\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n''',
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n            return false;\n        }\n        return fprintf(output,\n                       "  %%%" PRIu32 " = global.addr @%s\\n",\n                       instruction->result,\n                       function->globals[instruction->value.global_id].name) >= 0;\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        return fprintf(output,\n                       "  %%%" PRIu32 " = block.addr %%bb%" PRIu32 "\\n",\n                       instruction->result,\n                       instruction->value.block_id) >= 0;\n    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n''',
    "block-address dump",
)

# RV64 Core backend: materialize the exact Core basic-block assembly label.
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n               function->globals[instruction->value.global_id].name != NULL &&\n               function->globals[instruction->value.global_id].name_length != 0U;\n    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n''',
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n               function->globals[instruction->value.global_id].name != NULL &&\n               function->globals[instruction->value.global_id].name_length != 0U;\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        return instruction->value.block_id < function->block_count;\n    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n''',
    "block-address backend support",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (instruction->value.global_id >= function->global_count ||\n            fprintf(file, "  la t0, %s\\n", function->globals[instruction->value.global_id].name) <\n                0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n    case MINIC_CORE_INSTRUCTION_LOAD:\n''',
    '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (instruction->value.global_id >= function->global_count ||\n            fprintf(file, "  la t0, %s\\n", function->globals[instruction->value.global_id].name) <\n                0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        if (instruction->value.block_id >= function->block_count ||\n            fprintf(file,\n                    "  la t0, .L%s_core_bb%" PRIu32 "\\n",\n                    function->name,\n                    instruction->value.block_id) < 0) {\n            return false;\n        }\n        return store_core_value(file, frame, instruction->result, "t0");\n    case MINIC_CORE_INSTRUCTION_LOAD:\n''',
    "block-address emission",
)

# Lowering: map frontend label statement ids to stable Core basic blocks.  The map is lazy,
# so both forward and backward &&label references resolve to the exact block later used by label:.
replace_once(
    "src/core/core_lower.c",
    '''    MinicCoreFunction *function;\n    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n} MinicCoreLowerContext;\n''',
    '''    MinicCoreFunction *function;\n    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n    MinicCoreBlockId *label_blocks;\n    size_t label_block_count;\n} MinicCoreLowerContext;\n''',
    "label block map fields",
)

helper_anchor = '''static bool core_memory_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n\n'''
helper = '''static MinicCoreLowerStatus lower_label_block(MinicCoreLowerContext *context,\n                                                   MinicStatementId statement_id,\n                                                   MinicCoreBlockId *block_id) {\n    const MinicStatement *statement;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        context->function == NULL || context->label_blocks == NULL || block_id == NULL ||\n        statement_id >= context->label_block_count) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    statement = minic_c0_program_statement(context->body->program, statement_id);\n    if (statement == NULL || statement->kind != MINIC_STATEMENT_LABEL ||\n        statement->target_statement != MINIC_STATEMENT_INVALID) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (context->label_blocks[statement_id] == MINIC_CORE_BLOCK_INVALID) {\n        if (!minic_core_function_add_block(context->function,\n                                           &context->label_blocks[statement_id])) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n    }\n    *block_id = context->label_blocks[statement_id];\n    return MINIC_CORE_LOWER_OK;\n}\n\n'''
replace_once(
    "src/core/core_lower.c",
    helper_anchor,
    helper_anchor + helper,
    "label block helper",
)

replace_once(
    "src/core/core_lower.c",
    '''    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\n''',
    '''    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {\n        MinicCoreBlockId target_block;\n        MinicCoreLowerStatus status;\n        MinicType pointee;\n\n        if (!minic_type_pointee(expression->type, &pointee) || !minic_type_is_void(pointee)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_label_block(context, expression->value.label_statement_id, &target_block);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS;\n        instruction.span = expression->span;\n        instruction.type = expression->type;\n        instruction.result = MINIC_CORE_VALUE_INVALID;\n        instruction.value.block_id = target_block;\n        return minic_core_function_append_value_instruction(\n                   context->function, context->block_id, &instruction, value_id)\n                   ? MINIC_CORE_LOWER_OK\n                   : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_DISCARD) {\n''',
    "label-address expression lowering",
)

replace_once(
    "src/core/core_lower.c",
    '''        if (statement->kind == MINIC_STATEMENT_LABEL) {\n            const MinicStatement *loop;\n            MinicStatementId next_statement_id;\n\n            if (statement_index + 1U >= source_block->statement_count) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            next_statement_id = source_block->statements[statement_index + 1U];\n            loop = minic_c0_program_statement(context->body->program, next_statement_id);\n            if (!internal_while_label_pair(statement, loop)) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            status = lower_while(context, loop, &statement_terminated);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            statement_index += 1U;\n        } else {\n''',
    '''        if (statement->kind == MINIC_STATEMENT_LABEL) {\n            const MinicStatement *loop;\n            MinicStatementId statement_id;\n            MinicStatementId next_statement_id;\n\n            statement_id = source_block->statements[statement_index];\n            next_statement_id = statement_index + 1U < source_block->statement_count\n                                    ? source_block->statements[statement_index + 1U]\n                                    : MINIC_STATEMENT_INVALID;\n            loop = next_statement_id == MINIC_STATEMENT_INVALID\n                       ? NULL\n                       : minic_c0_program_statement(context->body->program, next_statement_id);\n            if (internal_while_label_pair(statement, loop)) {\n                status = lower_while(context, loop, &statement_terminated);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n                statement_index += 1U;\n            } else {\n                MinicCoreBlockId label_block;\n\n                status = lower_label_block(context, statement_id, &label_block);\n                if (status != MINIC_CORE_LOWER_OK) {\n                    return status;\n                }\n                if (context->block_id != label_block) {\n                    status = set_branch(context, context->block_id, statement->span, label_block);\n                    if (status != MINIC_CORE_LOWER_OK) {\n                        return status;\n                    }\n                    context->block_id = label_block;\n                }\n            }\n        } else {\n''',
    "user label statement lowering",
)

replace_once(
    "src/core/core_lower.c",
    '''    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n    MinicCoreLowerStatus status;\n    size_t local_index;\n    bool terminated;\n''',
    '''    MinicCoreBlockId block_id;\n    MinicCoreBlockId *label_blocks;\n    MinicCoreObjectId *local_objects;\n    MinicCoreLowerStatus status;\n    size_t label_index;\n    size_t local_index;\n    bool terminated;\n''',
    "function label map locals",
)
replace_once(
    "src/core/core_lower.c",
    '''    if (source_function->local_count > SIZE_MAX / sizeof(*local_objects)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    local_objects =\n''',
    '''    if (source_function->local_count > SIZE_MAX / sizeof(*local_objects) ||\n        body->program->statement_count > SIZE_MAX / sizeof(*label_blocks)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    label_blocks = body->program->statement_count == 0U\n                       ? NULL\n                       : (MinicCoreBlockId *)malloc(body->program->statement_count *\n                                                    sizeof(*label_blocks));\n    if (body->program->statement_count != 0U && label_blocks == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    for (label_index = 0U; label_index < body->program->statement_count; ++label_index) {\n        label_blocks[label_index] = MINIC_CORE_BLOCK_INVALID;\n    }\n    local_objects =\n''',
    "allocate label map",
)
replace_once(
    "src/core/core_lower.c",
    '''    if (source_function->local_count != 0U && local_objects == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n''',
    '''    if (source_function->local_count != 0U && local_objects == NULL) {\n        free(label_blocks);\n        return MINIC_CORE_LOWER_ERROR;\n    }\n''',
    "free label map on local allocation failure",
)
replace_once(
    "src/core/core_lower.c",
    '''        !minic_core_function_add_block(&lowered, &block_id)) {\n        free(local_objects);\n        minic_core_function_destroy(&lowered);\n''',
    '''        !minic_core_function_add_block(&lowered, &block_id)) {\n        free(label_blocks);\n        free(local_objects);\n        minic_core_function_destroy(&lowered);\n''',
    "free label map on setup failure",
)
replace_once(
    "src/core/core_lower.c",
    '''    context.block_id = block_id;\n    context.local_objects = local_objects;\n    status = lower_parameter_ingress(&context);\n''',
    '''    context.block_id = block_id;\n    context.local_objects = local_objects;\n    context.label_blocks = label_blocks;\n    context.label_block_count = body->program->statement_count;\n    status = lower_parameter_ingress(&context);\n''',
    "attach label map",
)
replace_once(
    "src/core/core_lower.c",
    '''    free(local_objects);\n    if (status != MINIC_CORE_LOWER_OK) {\n''',
    '''    free(label_blocks);\n    free(local_objects);\n    if (status != MINIC_CORE_LOWER_OK) {\n''',
    "free label map after lowering",
)

print("M44_PATCH_APPLIED")
