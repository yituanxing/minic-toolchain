#include "target/riscv64/codegen_internal.h"

static bool minic_riscv64_emit_statement(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicStatement *statement,
    size_t *label_counter)
{
    if (statement == NULL) {
        return false;
    }

    switch (statement->kind) {
    case MINIC_STATEMENT_ASSIGN:
        return minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               minic_riscv64_emit_local_store(
                   file,
                   function,
                   statement->local_id);

    case MINIC_STATEMENT_RETURN:
        return minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               fprintf(file, "  j .L%s_return\n", function->name) >= 0;

    case MINIC_STATEMENT_IF: {
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                statement->expression) ||
            fprintf(file, "  beqz a0, .Lif_else_%zu\n", label) < 0 ||
            !minic_riscv64_emit_block(
                file,
                program,
                function,
                statement->then_block,
                label_counter) ||
            fprintf(
                file,
                "  j .Lif_end_%zu\n"
                ".Lif_else_%zu:\n",
                label,
                label) < 0) {
            return false;
        }
        if (statement->else_block != MINIC_BLOCK_INVALID &&
            !minic_riscv64_emit_block(
                file,
                program,
                function,
                statement->else_block,
                label_counter)) {
            return false;
        }
        return fprintf(file, ".Lif_end_%zu:\n", label) >= 0;
    }

    case MINIC_STATEMENT_WHILE: {
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        return fprintf(file, ".Lwhile_condition_%zu:\n", label) >= 0 &&
               minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               fprintf(file, "  beqz a0, .Lwhile_end_%zu\n", label) >= 0 &&
               minic_riscv64_emit_block(
                   file,
                   program,
                   function,
                   statement->then_block,
                   label_counter) &&
               fprintf(
                   file,
                   "  j .Lwhile_condition_%zu\n"
                   ".Lwhile_end_%zu:\n",
                   label,
                   label) >= 0;
    }
    }

    return false;
}

bool minic_riscv64_emit_block(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicBlockId block_id,
    size_t *label_counter)
{
    const MinicBlock *block;
    size_t index;

    block = minic_c0_program_block(program, block_id);
    if (block == NULL) {
        return false;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(
            program,
            block->statements[index]);
        if (!minic_riscv64_emit_statement(
                file,
                program,
                function,
                statement,
                label_counter)) {
            return false;
        }
    }
    return true;
}
