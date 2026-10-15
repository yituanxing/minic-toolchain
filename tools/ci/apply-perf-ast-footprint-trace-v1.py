#!/usr/bin/env python3
"""Opt-in, stderr-only structural counter for real preprocessed Kbuild .i.

Read-only after parse; no AST transformation, no altered token allocation,
no codegen pass or constant propagation changes. Output bytes are checked
against the identical uninstrumented Runtime compiler on pinned .i.
"""
from pathlib import Path

p=Path("src/compiler/compiler.c")
source=p.read_text()
inc='#include "frontend/parser.h"\n'
if source.count(inc)!=1:
    raise SystemExit("AST_FOOTPRINT: parser include anchor mismatch")
source=source.replace(inc,inc+'#include "frontend/parser_internal.h"\n',1)
needle='    success = minic_parse_c0_program(input_path, buffer.data, buffer.size, &program, diagnostic);\n'
if source.count(needle)!=1:
    raise SystemExit("AST_FOOTPRINT: parse anchor mismatch")
insert=r'''    if (success && getenv("MINIC_AST_FOOTPRINT") != NULL) {
        size_t expression_index;
        size_t call_count = 0U;
        size_t call_arg_count = 0U;
        size_t function_parameter_count = 0U;
        for (expression_index = 0U;
             expression_index < program.expression_count; ++expression_index) {
            const MinicExpression *expression = &program.expressions[expression_index];
            if (expression->kind == MINIC_EXPRESSION_CALL) {
                call_count += 1U;
                call_arg_count += expression->value.call.argument_count;
            }
        }
        for (expression_index = 0U;
             expression_index < program.function_count; ++expression_index) {
            function_parameter_count += program.functions[expression_index].parameter_count;
        }
        (void)fprintf(stderr,
          "MINIC_AST_FOOTPRINT input_bytes=%zu expressions=%zu expression_capacity=%zu"
          " expression_bytes=%zu expression_allocated_bytes=%zu"
          " call_expr=%zu call_arg_used=%zu wasted_noncalls_minimum_bytes=%zu"
          " functions=%zu function_capacity=%zu function_bytes=%zu"
          " function_params_used=%zu function_param_reserved=%zu"
          " statements=%zu blocks=%zu globals=%zu type_aliases=%zu"
          " sizeof_type=%zu sizeof_parser=%zu sizeof_switch_context=%zu\n",
          buffer.size, program.expression_count, program.expression_capacity,
          sizeof(MinicExpression),
          program.expression_capacity * sizeof(MinicExpression),
          call_count, call_arg_count,
          (program.expression_count - call_count) * sizeof(MinicExpressionId) *
              MINIC_MAX_FUNCTION_PARAMETERS,
          program.function_count, program.function_capacity,
          sizeof(MinicFunction),
          function_parameter_count,
          program.function_count * MINIC_MAX_FUNCTION_PARAMETERS,
          program.statement_count, program.block_count,
          program.global_object_count, program.type_alias_count,
          sizeof(MinicType), sizeof(MinicParser), sizeof(MinicParserSwitchContext));
        (void)fflush(stderr);
    }
'''
source=source.replace(needle,needle+insert,1)
p.write_text(source)
print("MINIC_AST_FOOTPRINT_TRACE_V1=APPLIED")
