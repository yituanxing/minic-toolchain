#!/usr/bin/env python3
from pathlib import Path

compiler_path = Path("src/compiler/compiler.c")
compiler_text = compiler_path.read_text()
compiler_old = '''        status = candidates->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK &&
            !minic_core_function_verify(&candidates->functions[function_index])) {
            status = MINIC_CORE_LOWER_ERROR;
        }
'''
compiler_new = '''        status = candidates->statuses[function_index];
        if (status == MINIC_CORE_LOWER_OK) {
            bool verified = minic_core_function_verify(&candidates->functions[function_index]);
            (void)fprintf(stderr,
                          "CORE_M19_DIAG function=%s lower_status=%d verify=%d\\n",
                          function->name,
                          (int)status,
                          verified ? 1 : 0);
            if (!verified) {
                status = MINIC_CORE_LOWER_ERROR;
            }
        } else {
            (void)fprintf(stderr,
                          "CORE_M19_DIAG function=%s lower_status=%d verify=na\\n",
                          function->name,
                          (int)status);
        }
'''
if compiler_text.count(compiler_old) != 1:
    raise SystemExit("Core validation diagnostic anchor changed")
compiler_path.write_text(compiler_text.replace(compiler_old, compiler_new, 1))

lower_path = Path("src/core/core_lower.c")
lower_text = lower_path.read_text()
include_old = '''#include <stdlib.h>\n#include <string.h>\n'''
include_new = '''#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'''
if lower_text.count(include_old) != 1:
    raise SystemExit("Core lowering include diagnostic anchor changed")
lower_text = lower_text.replace(include_old, include_new, 1)

statement_old = '''        status = lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status =
            lower_expression(context, expression->value.statement_expression.result, &result_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (result_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[result_value].type, result_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
statement_new = '''        status = lower_block(context, statement_block, &terminated);
        if (context->source_function != NULL && context->source_function->name != NULL &&
            strcmp(context->source_function->name, "list_empty_careful") == 0 &&
            (status != MINIC_CORE_LOWER_OK || terminated)) {
            (void)fprintf(stderr,
                          "Core IR shadow M19_STMT expression=%zu block=%zu stage=block status=%d terminated=%d continuation=%u\\n",
                          expression_id,
                          expression->value.statement_expression.block,
                          (int)status,
                          terminated ? 1 : 0,
                          (unsigned int)context->block_id);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status =
            lower_expression(context, expression->value.statement_expression.result, &result_value);
        if (context->source_function != NULL && context->source_function->name != NULL &&
            strcmp(context->source_function->name, "list_empty_careful") == 0 &&
            status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "Core IR shadow M19_STMT expression=%zu stage=result status=%d result=%u continuation=%u values=%zu\\n",
                          expression_id,
                          (int)status,
                          (unsigned int)result_value,
                          (unsigned int)context->block_id,
                          context->function->value_count);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->source_function != NULL && context->source_function->name != NULL &&
            strcmp(context->source_function->name, "list_empty_careful") == 0 &&
            (result_value >= context->function->value_count ||
             !minic_type_equal(context->function->values[result_value].type, result_type))) {
            (void)fprintf(stderr,
                          "Core IR shadow M19_STMT expression=%zu stage=type result_in_range=%d type_equal=%d\\n",
                          expression_id,
                          result_value < context->function->value_count ? 1 : 0,
                          result_value < context->function->value_count &&
                                  minic_type_equal(context->function->values[result_value].type,
                                                   result_type)
                              ? 1
                              : 0);
        }
        if (result_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[result_value].type, result_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
if lower_text.count(statement_old) != 1:
    raise SystemExit("Core statement-expression diagnostic anchor changed")
lower_path.write_text(lower_text.replace(statement_old, statement_new, 1))
print("instrumented Core M19 failing statement-expression stage")
