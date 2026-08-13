#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
old = '''            (void)aggregate_size;
            if (value->value_category == MINIC_VALUE_LVALUE) {
                if (!minic_riscv64_emit_lvalue_address(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n  ld a0, 0(t0)\\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\\n") < 0)) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, statement->expression)) {
                return false;
            }
'''
new = '''            (void)aggregate_size;
            if (minic_c0_record_value_is_address_backed(program, statement->expression)) {
                if (!minic_riscv64_emit_address_backed_record_value(
                        file, program, function, statement->expression) ||
                    fprintf(file, "  mv t0, a0\\n  ld a0, 0(t0)\\n") < 0 ||
                    (aggregate_chunks == 2U && fprintf(file, "  ld a1, 8(t0)\\n") < 0)) {
                    return false;
                }
            } else if (value->kind != MINIC_EXPRESSION_CALL ||
                       !minic_riscv64_emit_expression(
                           file, program, function, statement->expression)) {
                return false;
            }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("record return source anchor not found")
path.write_text(text)
