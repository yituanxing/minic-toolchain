#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}")
    path.write_text(text.replace(old, new, 1))


codegen = Path("src/target/riscv64/codegen_inline_asm.c")
replace_once(
    codegen,
    '''    if (constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {\n        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&\n               expression->kind == MINIC_EXPRESSION_LOCAL &&\n               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));\n    }\n''',
    '''    if (constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {\n        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&\n               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));\n    }\n''',
)
replace_once(
    codegen,
    '''        if (constraint_is(operand, "+A") || constraint_is(operand, "=m")) {\n            if (!minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, operand->expression) ||\n                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {\n                return false;\n            }\n        } else if (constraint_is(operand, "+r")) {\n''',
    '''        if (constraint_is(operand, "+A") || constraint_is(operand, "=m") ||\n            constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {\n            if (!minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, operand->expression) ||\n                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {\n                return false;\n            }\n        } else if (constraint_is(operand, "+r")) {\n''',
)
replace_once(
    codegen,
    '''        expression = minic_c0_program_expression(program, operand->expression);\n        if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||\n            !minic_riscv64_emit_object_store_register(file,\n                                                      program,\n                                                      function,\n                                                      function_layout,\n                                                      expression->value.local_id,\n                                                      operand_registers[index])) {\n            return false;\n        }\n''',
    '''        expression = minic_c0_program_expression(program, operand->expression);\n        if (expression == NULL) {\n            return false;\n        }\n        if (constraint_is(operand, "+r")) {\n            if (expression->kind != MINIC_EXPRESSION_LOCAL ||\n                !minic_riscv64_emit_object_store_register(file,\n                                                          program,\n                                                          function,\n                                                          function_layout,\n                                                          expression->value.local_id,\n                                                          operand_registers[index])) {\n                return false;\n            }\n        } else if (!minic_riscv64_emit_sp_load64(file, "a0", index * 8U) ||\n                   !minic_riscv64_emit_scalar_store(\n                       file, expression->type, operand_registers[index], "a0")) {\n            return false;\n        }\n''',
)

fixture = Path("tests/compiler/c0/gnu_inline_asm_operands.c")
replace_once(
    fixture,
    '''static int linux_target_constraint_shape(int value) {\n''',
    '''typedef struct RegisterOutputs {\n    unsigned long first;\n    unsigned long second;\n    unsigned long third;\n    unsigned long fourth;\n    unsigned long fifth;\n} RegisterOutputs;\n\nstatic void register_member_outputs_like(RegisterOutputs *target) {\n    __asm__ __volatile__("li %0, 1\\n\\t"\n                         "li %1, 2\\n\\t"\n                         "li %2, 3\\n\\t"\n                         "li %3, 4\\n\\t"\n                         "li %4, 5"\n                         : "=r"(target->first), "=r"(target->second), "=r"(target->third),\n                           "=r"(target->fourth), "=r"(target->fifth));\n}\n\nstatic int linux_target_constraint_shape(int value) {\n''',
)

runner = Path("tests/compiler/c0/run-gnu-inline-asm-operands.sh")
replace_once(
    runner,
    '''grep -F 'sw t1, 0(t0)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null\n''',
    '''grep -F 'sw t1, 0(t0)' "$assembly" >/dev/null\ngrep -F '.type register_member_outputs_like, @function' "$assembly" >/dev/null\ngrep -F 'li t0, 1' "$assembly" >/dev/null\ngrep -F 'li t1, 2' "$assembly" >/dev/null\ngrep -F 'li t3, 3' "$assembly" >/dev/null\ngrep -F 'li t4, 4' "$assembly" >/dev/null\ngrep -F 'li t5, 5' "$assembly" >/dev/null\ngrep -F 'sd t5, 0(a0)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null\n''',
)
replace_once(
    runner,
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r inputs=r,rJ,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'\n''',
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r register-lvalue=local+member inputs=r,rJ,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'\n''',
)
