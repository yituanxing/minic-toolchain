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
    '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&\n               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&\n               !constraint_is(operand, "rK")) {\n        return false;\n    }\n    expression = minic_c0_program_expression(program, operand->expression);\n    if (expression == NULL) {\n        return false;\n    }\n    if (constraint_is(operand, "rJ")) {\n''',
    '''    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&\n               !constraint_is(operand, "i") && !constraint_is(operand, "rJ") &&\n               !constraint_is(operand, "rK") && !constraint_is(operand, "m")) {\n        return false;\n    }\n    expression = minic_c0_program_expression(program, operand->expression);\n    if (expression == NULL) {\n        return false;\n    }\n    if (constraint_is(operand, "m")) {\n        return expression->value_category == MINIC_VALUE_LVALUE;\n    }\n    if (constraint_is(operand, "rJ")) {\n''',
)
replace_once(
    codegen,
    '''            } else if (constraint_is(operand, "=m")) {\n                if (fprintf(file, "0(%s)", register_name) < 0) {\n                    return false;\n                }\n''',
    '''            } else if (constraint_is(operand, "=m") || constraint_is(operand, "m")) {\n                if (fprintf(file, "0(%s)", register_name) < 0) {\n                    return false;\n                }\n''',
)
replace_once(
    codegen,
    '''        if (operand_uses_immediate(program, operand)) {\n            continue;\n        }\n        if (!minic_riscv64_emit_expression(\n                file, program, function, function_layout, operand->expression) ||\n            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {\n            return false;\n        }\n''',
    '''        if (operand_uses_immediate(program, operand)) {\n            continue;\n        }\n        if (constraint_is(operand, "m")) {\n            if (!minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, operand->expression) ||\n                !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {\n                return false;\n            }\n            continue;\n        }\n        if (!minic_riscv64_emit_expression(\n                file, program, function, function_layout, operand->expression) ||\n            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {\n            return false;\n        }\n''',
)

fixture = Path("tests/compiler/c0/gnu_inline_asm_operands.c")
fixture.write_text(
    fixture.read_text()
    + '''\nstatic int memory_input_linux_shape(const int *value) {\n    long error = 0;\n    int loaded;\n\n    __asm__ __volatile__("lw %1, %2"\n                         : "+r"(error), "=&r"(loaded)\n                         : "m"(*value));\n    return loaded + (int)error;\n}\n'''
)

runner = Path("tests/compiler/c0/run-gnu-inline-asm-operands.sh")
replace_once(
    runner,
    '''grep -F 'sd t5, 0(a0)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null\n''',
    '''grep -F 'sd t5, 0(a0)' "$assembly" >/dev/null\ngrep -F '.type memory_input_linux_shape, @function' "$assembly" >/dev/null\ngrep -F 'lw t1, 0(t3)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null\n''',
)
replace_once(
    runner,
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r register-lvalue=local+member inputs=r,rJ,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'\n''',
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r register-lvalue=local+member inputs=r,rJ,I,m memory-input=lvalue clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'\n''',
)
