#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_support.c")
text = path.read_text()
old = '''            if (!minic_riscv64_integer_aggregate_abi(
                    program, parameter->type, &aggregate_size, &aggregate_chunks) ||
                integer_parameter_count > SIZE_MAX - aggregate_chunks) {
                return false;
            }
'''
new = '''            if (!minic_riscv64_integer_aggregate_abi(
                    program, parameter->type, &aggregate_size, &aggregate_chunks)) {
                const MinicRecord *record;
                size_t size;
                size_t alignment;
                size_t field_index;

                record = minic_c0_program_record(program, parameter->type.record_id);
                size = 0U;
                alignment = 0U;
                (void)minic_riscv64_type_layout(program, parameter->type, &size, &alignment);
                fprintf(stderr,
                        "CODEGEN_FRAME_AGG_REJECT param=%zu record=%zu size=%zu align=%zu complete=%d fields=%zu\\n",
                        parameter_index,
                        parameter->type.record_id,
                        size,
                        alignment,
                        record != NULL && record->is_complete ? 1 : 0,
                        record == NULL ? 0U : record->field_count);
                if (record != NULL) {
                    for (field_index = 0U; field_index < record->field_count; ++field_index) {
                        const MinicRecordField *field;
                        field = minic_c0_record_field(record, field_index);
                        fprintf(stderr,
                                "CODEGEN_FRAME_AGG_FIELD index=%zu kind=%d ptr=%u array=%d record=%zu count=%zu\\n",
                                field_index,
                                field == NULL ? -1 : (int)field->type.base_kind,
                                field == NULL ? 0U : field->type.pointer_depth,
                                field != NULL && minic_type_is_array(field->type) ? 1 : 0,
                                field == NULL ? SIZE_MAX : field->type.record_id,
                                field == NULL ? 0U : field->element_count);
                    }
                }
                return false;
            }
            if (integer_parameter_count > SIZE_MAX - aggregate_chunks) {
                return false;
            }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("frame aggregate trace anchor not found")
path.write_text(text)

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = '''    case MINIC_STATEMENT_EXPRESSION:
        return statement->expression != MINIC_EXPRESSION_INVALID &&
               minic_riscv64_emit_expression(file, program, function, statement->expression);
'''
new = '''    case MINIC_STATEMENT_EXPRESSION: {
        const MinicExpression *expression;

        expression = minic_c0_program_expression(program, statement->expression);
        if (statement->expression == MINIC_EXPRESSION_INVALID || expression == NULL) {
            return false;
        }
        if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
            fprintf(stderr,
                    "CODEGEN_EXPR_STMT_FAIL expr=%zu kind=%d type=%d/%u vcat=%d line=%zu column=%zu\\n",
                    (size_t)statement->expression,
                    (int)expression->kind,
                    (int)expression->type.base_kind,
                    expression->type.pointer_depth,
                    (int)expression->value_category,
                    expression->span.begin.line,
                    expression->span.begin.column);
            return false;
        }
        return true;
    }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("expression statement trace anchor not found")
path.write_text(text)
