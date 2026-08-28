from pathlib import Path

MARKER = 'M64_LOCAL_LABEL_BLOCK_ADDRESS'


def replace_once(text: str, anchor: str, replacement: str, label: str) -> str:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'M64 {label} anchor count={count}')
    return text.replace(anchor, replacement, 1)


# Core IR: a local label address is a target-neutral address of a Core block.
path = Path('src/core/core_ir.h')
text = path.read_text()
if MARKER not in text:
    text = replace_once(text,
        '    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n',
        '    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,\n    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: target-neutral address of a Core basic block. */\n    MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS,\n    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,\n', 'ir-kind')
    text = replace_once(text,
        '        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        struct {\n',
        '        MinicCoreObjectId object_id;\n        MinicCoreGlobalId global_id;\n        MinicCoreBlockId block_id;\n        struct {\n', 'ir-payload')
    path.write_text(text)
else:
    print('M64 core_ir.h already applied')

# Core verifier + dump.
path = Path('src/core/core_ir.c')
text = path.read_text()
if MARKER not in text:
    anchor = '    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n'
    repl = '    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: block addresses are first-class pointer values. */\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        return instruction_result_is_valid(function, instruction) &&\n               minic_type_is_pointer(instruction->type) &&\n               instruction->value.block_id < function->block_count;\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {\n        MinicType pointer_type;\n'
    text = replace_once(text, anchor, repl, 'verifier')
    anchor = '    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n'
    repl = '    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        return fprintf(output,\n                       "  %%%" PRIu32 " = block.addr %%bb%" PRIu32 "\\n",\n                       instruction->result, instruction->value.block_id) >= 0;\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        if (function == NULL || instruction->value.global_id >= function->global_count) {\n'
    text = replace_once(text, anchor, repl, 'dump')
    path.write_text(text)
else:
    print('M64 core_ir.c already applied')

# Lowering: semantic statement -> Core block mapping and &&label lowering.
path = Path('src/core/core_lower.c')
text = path.read_text()
if MARKER not in text:
    text = replace_once(text,
        '    MinicCoreFunction *function;\n    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n} MinicCoreLowerContext;\n',
        '    MinicCoreFunction *function;\n    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: semantic statement -> Core block map. */\n    MinicCoreBlockId *statement_blocks;\n    size_t statement_block_count;\n} MinicCoreLowerContext;\n', 'context')
    text = replace_once(text,
        '    *value_type = expression->type;\n    return true;\n}\n\nstatic MinicCoreLowerStatus lower_local_object(MinicCoreLowerContext *context,\n',
        '    *value_type = expression->type;\n    return true;\n}\n\nstatic MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context, MinicStatementId statement_id, MinicCoreBlockId *block_id) {\n    MinicCoreBlockId mapped;\n    if (context == NULL || context->function == NULL || block_id == NULL || context->statement_blocks == NULL || statement_id >= context->statement_block_count) return MINIC_CORE_LOWER_ERROR;\n    mapped = context->statement_blocks[statement_id];\n    if (mapped == MINIC_CORE_BLOCK_INVALID) {\n        if (!minic_core_function_add_block(context->function, &mapped)) return MINIC_CORE_LOWER_ERROR;\n        context->statement_blocks[statement_id] = mapped;\n    }\n    *block_id = mapped;\n    return MINIC_CORE_LOWER_OK;\n}\n\nstatic MinicCoreLowerStatus lower_local_object(MinicCoreLowerContext *context,\n', 'statement-block-helper')
    text = replace_once(text,
        '        *value_id = MINIC_CORE_VALUE_INVALID;\n        return MINIC_CORE_LOWER_OK;\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n',
        '        *value_id = MINIC_CORE_VALUE_INVALID;\n        return MINIC_CORE_LOWER_OK;\n    }\n    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {\n        const MinicStatement *label_statement; MinicCoreBlockId label_block; MinicCoreLowerStatus status;\n        label_statement = minic_c0_program_statement(context->body->program, expression->value.label_statement_id);\n        if (label_statement == NULL) return MINIC_CORE_LOWER_ERROR;\n        if (label_statement->kind != MINIC_STATEMENT_LABEL || !minic_type_is_pointer(expression->type)) return MINIC_CORE_LOWER_UNSUPPORTED;\n        status = ensure_statement_block(context, expression->value.label_statement_id, &label_block);\n        if (status != MINIC_CORE_LOWER_OK) return status;\n        (void)memset(&instruction, 0, sizeof(instruction));\n        instruction.kind = MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS; instruction.span = expression->span; instruction.type = expression->type; instruction.result = MINIC_CORE_VALUE_INVALID; instruction.value.block_id = label_block;\n        return minic_core_function_append_value_instruction(context->function, context->block_id, &instruction, value_id) ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n', 'label-address')
    old = '''        if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop;
            MinicStatementId next_statement_id;

            if (statement_index + 1U >= source_block->statement_count) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            next_statement_id = source_block->statements[statement_index + 1U];
            loop = minic_c0_program_statement(context->body->program, next_statement_id);
            if (!internal_while_label_pair(statement, loop)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_while(context, loop, &statement_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            statement_index += 1U;
        } else {
'''
    new = '''        if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop = NULL;
            bool internal_loop_label = false;
            if (statement_index + 1U < source_block->statement_count) {
                MinicStatementId next_statement_id = source_block->statements[statement_index + 1U];
                loop = minic_c0_program_statement(context->body->program, next_statement_id);
                internal_loop_label = internal_while_label_pair(statement, loop);
            }
            if (internal_loop_label) {
                status = lower_while(context, loop, &statement_terminated);
                if (status != MINIC_CORE_LOWER_OK) return status;
                statement_index += 1U;
            } else {
                MinicCoreBlockId label_block;
                MinicStatementId label_statement_id = source_block->statements[statement_index];
                if (statement->target_expression != MINIC_EXPRESSION_INVALID || statement->expression != MINIC_EXPRESSION_INVALID || statement->target_statement != MINIC_STATEMENT_INVALID) return MINIC_CORE_LOWER_UNSUPPORTED;
                status = ensure_statement_block(context, label_statement_id, &label_block);
                if (status != MINIC_CORE_LOWER_OK) return status;
                if (context->block_id != label_block) {
                    status = set_branch(context, context->block_id, statement->span, label_block);
                    if (status != MINIC_CORE_LOWER_OK) return status;
                }
                context->block_id = label_block;
            }
        } else {
'''
    text = replace_once(text, old, new, 'ordinary-label')
    text = replace_once(text,
        '    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n    MinicCoreLowerStatus status;\n    size_t local_index;\n    bool terminated;\n',
        '    MinicCoreBlockId block_id;\n    MinicCoreObjectId *local_objects;\n    MinicCoreBlockId *statement_blocks;\n    MinicCoreLowerStatus status;\n    size_t local_index;\n    size_t statement_index;\n    bool terminated;\n', 'function-decls')
    text = replace_once(text,
        '    for (local_index = 0U; local_index < source_function->local_count; ++local_index) {\n        local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;\n    }\n\n    minic_core_function_initialize(&lowered);\n',
        '    for (local_index = 0U; local_index < source_function->local_count; ++local_index) local_objects[local_index] = MINIC_CORE_OBJECT_INVALID;\n    if (body->program->statement_count > SIZE_MAX / sizeof(*statement_blocks)) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }\n    statement_blocks = body->program->statement_count == 0U ? NULL : (MinicCoreBlockId *)malloc(body->program->statement_count * sizeof(*statement_blocks));\n    if (body->program->statement_count != 0U && statement_blocks == NULL) { free(local_objects); return MINIC_CORE_LOWER_ERROR; }\n    for (statement_index = 0U; statement_index < body->program->statement_count; ++statement_index) statement_blocks[statement_index] = MINIC_CORE_BLOCK_INVALID;\n\n    minic_core_function_initialize(&lowered);\n', 'map-allocation')
    text = replace_once(text,
        '        !minic_core_function_add_block(&lowered, &block_id)) {\n        free(local_objects);\n        minic_core_function_destroy(&lowered);\n        return MINIC_CORE_LOWER_ERROR;\n    }\n',
        '        !minic_core_function_add_block(&lowered, &block_id)) {\n        free(statement_blocks); free(local_objects); minic_core_function_destroy(&lowered); return MINIC_CORE_LOWER_ERROR;\n    }\n', 'setup-cleanup')
    text = replace_once(text,
        '    context.function = &lowered;\n    context.block_id = block_id;\n    context.local_objects = local_objects;\n    status = lower_parameter_ingress(&context);\n',
        '    context.function = &lowered;\n    context.block_id = block_id;\n    context.local_objects = local_objects;\n    context.statement_blocks = statement_blocks;\n    context.statement_block_count = body->program->statement_count;\n    status = lower_parameter_ingress(&context);\n', 'context-map')
    text = replace_once(text,
        '    if (status == MINIC_CORE_LOWER_OK) {\n        status = lower_block(&context, source_block, &terminated);\n    }\n    free(local_objects);\n    if (status != MINIC_CORE_LOWER_OK) {\n',
        '    if (status == MINIC_CORE_LOWER_OK) status = lower_block(&context, source_block, &terminated);\n    free(statement_blocks); free(local_objects);\n    if (status != MINIC_CORE_LOWER_OK) {\n', 'cleanup')
    path.write_text(text)
else:
    print('M64 core_lower.c already applied')

# RV64 block-address emission.
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
if MARKER not in text:
    text = replace_once(text,
        '    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n',
        '    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: RV64 spells the Core block label. */\n    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n        return minic_type_is_pointer(instruction->type) && instruction->value.block_id < function->block_count;\n    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n        return instruction->value.global_id < function->global_count &&\n', 'rv64-support')
    text = replace_once(text,
        'static bool emit_instruction(FILE *file,\n                             const MinicC0Program *program,\n                             const MinicCoreFunction *function,\n                             const MinicRiscv64CoreFrame *frame,\n                             const MinicCoreInstruction *instruction) {\n',
        'static bool emit_instruction(FILE *file,\n                             const MinicC0Program *program,\n                             const MinicCoreFunction *function,\n                             const MinicRiscv64CoreFrame *frame,\n                             const char *symbol_name,\n                             const MinicCoreInstruction *instruction) {\n', 'emit-signature')
    text = replace_once(text,
        '    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||\n        !core_instruction_supported(program, function, instruction)) {\n',
        '    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL || instruction == NULL ||\n        !core_instruction_supported(program, function, instruction)) {\n', 'emit-guard')
    anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  la t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
    repl = '''    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        if (instruction->value.block_id >= function->block_count ||
            fprintf(file, "  la t0, .L%s_core_bb%" PRIu32 "\n", symbol_name, instruction->value.block_id) < 0) return false;
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  la t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
    text = replace_once(text, anchor, repl, 'rv64-emit')
    text = replace_once(text,
        '                !emit_instruction(\n                    file, program, function, &frame, &function->instructions[instruction_id])) {\n',
        '                !emit_instruction(file, program, function, &frame, symbol_name, &function->instructions[instruction_id])) {\n', 'emit-call')
    path.write_text(text)
else:
    print('M64 core_codegen.c already applied')

print('M64 local label / block address applied')
