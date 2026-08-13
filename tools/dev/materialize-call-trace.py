#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text()

old = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                return false;
            }
'''
new = r'''            if (!minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                fprintf(stderr,
                        "CODEGEN_CALL_ARG scalar caller=%s callee=%s arg=%zu expr=%zu kind=%d type=%d/%u vcat=%d fixed=%d abi_type=%d/%u\n",
                        function != NULL ? function->name : "<null>",
                        direct_callee != NULL ? direct_callee->name : "<indirect>",
                        argument_index,
                        (size_t)expression->value.call.arguments[argument_index],
                        (int)argument->kind,
                        (int)argument->type.base_kind,
                        argument->type.pointer_depth,
                        (int)argument->value_category,
                        argument_index < parameter_count ? 1 : 0,
                        argument_index < parameter_count ? (int)abi_parameter_types[argument_index].base_kind : -1,
                        argument_index < parameter_count ? abi_parameter_types[argument_index].pointer_depth : 0U);
                return false;
            }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("scalar call argument trace anchor not found")
path.write_text(text)

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = r'''        } else if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
'''
new = r'''        } else if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            const MinicExpression *failed_value =
                minic_c0_program_expression(program, statement->expression);
            fprintf(stderr,
                    "CODEGEN_RETURN_STAGE value function=%s expr=%zu kind=%d type=%d/%u vcat=%d cleanup=%zu->%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)statement->expression,
                    failed_value == NULL ? -1 : (int)failed_value->kind,
                    failed_value == NULL ? -1 : (int)failed_value->type.base_kind,
                    failed_value == NULL ? 0U : failed_value->type.pointer_depth,
                    failed_value == NULL ? -1 : (int)failed_value->value_category,
                    (size_t)statement->cleanup_context,
                    (size_t)statement->cleanup_stop_context);
            return false;
        }
        if (minic_type_is_integer(function->return_type) &&
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("return value trace anchor not found")

old = r'''        if (!minic_riscv64_emit_cleanup_contexts(file,
                                                 program,
                                                 function,
                                                 statement->cleanup_context,
                                                 statement->cleanup_stop_context)) {
            return false;
        }
'''
new = r'''        if (!minic_riscv64_emit_cleanup_contexts(file,
                                                 program,
                                                 function,
                                                 statement->cleanup_context,
                                                 statement->cleanup_stop_context)) {
            fprintf(stderr,
                    "CODEGEN_RETURN_STAGE cleanup function=%s expr=%zu cleanup=%zu->%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)statement->expression,
                    (size_t)statement->cleanup_context,
                    (size_t)statement->cleanup_stop_context);
            return false;
        }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("return cleanup trace anchor not found")

old = r'''        context = minic_c0_program_cleanup_context(program, current);
        if (context == NULL ||
            !minic_riscv64_emit_expression(file, program, function, context->cleanup_expression)) {
            return false;
        }
        current = context->parent;
'''
new = r'''        context = minic_c0_program_cleanup_context(program, current);
        if (context == NULL) {
            fprintf(stderr,
                    "CODEGEN_CLEANUP_FAIL function=%s context=%zu missing=1\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)current);
            return false;
        }
        if (!minic_riscv64_emit_expression(file, program, function, context->cleanup_expression)) {
            const MinicExpression *cleanup =
                minic_c0_program_expression(program, context->cleanup_expression);
            fprintf(stderr,
                    "CODEGEN_CLEANUP_FAIL function=%s context=%zu expr=%zu kind=%d type=%d/%u vcat=%d parent=%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)current,
                    (size_t)context->cleanup_expression,
                    cleanup == NULL ? -1 : (int)cleanup->kind,
                    cleanup == NULL ? -1 : (int)cleanup->type.base_kind,
                    cleanup == NULL ? 0U : cleanup->type.pointer_depth,
                    cleanup == NULL ? -1 : (int)cleanup->value_category,
                    (size_t)context->parent);
            return false;
        }
        current = context->parent;
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("cleanup expression trace anchor not found")
path.write_text(text)
