#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee == NULL || !callee->is_defined || !callee->is_internal || !callee->is_inline ||
'''
new = '''    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee != NULL && callee->name != NULL &&
        (strcmp(callee->name, "bio_has_crypt_ctx") == 0 ||
         strcmp(callee->name, "blk_crypto_rq_is_encrypted") == 0 ||
         strcmp(callee->name, "is_nd_pfn") == 0 ||
         strcmp(callee->name, "is_nd_dax") == 0 ||
         strcmp(callee->name, "pmd_trans_huge") == 0 ||
         strcmp(callee->name, "pmd_devmap") == 0 ||
         strcmp(callee->name, "pud_trans_huge") == 0 ||
         strcmp(callee->name, "pud_devmap") == 0 ||
         strcmp(callee->name, "pfn_t_devmap") == 0)) {
        const MinicBlock *probe_body = callee->body_block == MINIC_BLOCK_INVALID
                                           ? NULL
                                           : minic_c0_program_block(program, callee->body_block);
        const MinicStatement *probe_statement =
            probe_body != NULL && probe_body->statement_count == 1U
                ? minic_c0_program_statement(program, probe_body->statements[0])
                : NULL;
        MinicConstValue probe_value;
        bool probe_eval = probe_statement != NULL &&
                          probe_statement->kind == MINIC_STATEMENT_RETURN &&
                          probe_statement->expression != MINIC_EXPRESSION_INVALID &&
                          minic_const_eval_integer(program,
                                                   context->target,
                                                   probe_statement->expression,
                                                   &probe_value);
        (void)fprintf(stderr,
                      "CFG_CONST_PROBE name=%s defined=%d internal=%d inline=%d body=%zu stmt=%d eval=%d retint=%d exprint=%d argc=%zu\\n",
                      callee->name,
                      callee->is_defined ? 1 : 0,
                      callee->is_internal ? 1 : 0,
                      callee->is_inline ? 1 : 0,
                      probe_body == NULL ? 0U : probe_body->statement_count,
                      probe_statement == NULL ? -1 : (int)probe_statement->kind,
                      probe_eval ? 1 : 0,
                      minic_type_is_integer(callee->return_type) ? 1 : 0,
                      minic_type_is_integer(expression->type) ? 1 : 0,
                      expression->value.call.argument_count);
    }
    if (callee == NULL || !callee->is_defined || !callee->is_internal || !callee->is_inline ||
'''
if text.count(old) != 1:
    raise SystemExit(f"probe anchor expected one match, found {text.count(old)}")
p.write_text(text.replace(old, new, 1))
print("MINIC_CONSTANT_INLINE_CALL_CFG_PROBE_V0=APPLIED")
