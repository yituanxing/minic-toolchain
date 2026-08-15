#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

root = Path(__file__).resolve().parents[2]
codegen = root / "src/target/riscv64/codegen_inline_asm.c"
fixture = root / "tests/compiler/c0/gnu_inline_asm_operands.c"
runner = root / "tests/compiler/c0/run-gnu-inline-asm-operands.sh"

text = codegen.read_text()
text = replace_once(
    text,
    '''    if (constraint_is(operand, "+A")) {\n        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;\n    }\n    if (constraint_is(operand, "+r")) {''',
    '''    if (constraint_is(operand, "+A")) {\n        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;\n    }\n    if (constraint_is(operand, "=m")) {\n        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;\n    }\n    if (constraint_is(operand, "+r")) {''',
    "validate =m output",
)
text = replace_once(
    text,
    '''            } else if (constraint_is(operand, "+A")) {\n                if (fprintf(file, "(%s)", register_name) < 0) {\n                    return false;\n                }\n            } else if (fputs(register_name, file) == EOF) {''',
    '''            } else if (constraint_is(operand, "+A")) {\n                if (fprintf(file, "(%s)", register_name) < 0) {\n                    return false;\n                }\n            } else if (constraint_is(operand, "=m")) {\n                if (fprintf(file, "0(%s)", register_name) < 0) {\n                    return false;\n                }\n            } else if (fputs(register_name, file) == EOF) {''',
    "render =m operand",
)
text = replace_once(
    text,
    '''        if (constraint_is(operand, "+A")) {\n            if (!minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, operand->expression) ||\n                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {\n                return false;\n            }\n        } else if (constraint_is(operand, "+r")) {''',
    '''        if (constraint_is(operand, "+A") || constraint_is(operand, "=m")) {\n            if (!minic_riscv64_emit_lvalue_address(\n                    file, program, function, function_layout, operand->expression) ||\n                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {\n                return false;\n            }\n        } else if (constraint_is(operand, "+r")) {''',
    "stage =m address",
)
text = replace_once(
    text,
    '''        if ((constraint_is(&inline_asm->outputs[index], "+A") ||\n             constraint_is(&inline_asm->outputs[index], "+r")) &&\n            !minic_riscv64_emit_sp_load64(file, operand_registers[index], index * 8U)) {''',
    '''        if ((constraint_is(&inline_asm->outputs[index], "+A") ||\n             constraint_is(&inline_asm->outputs[index], "=m") ||\n             constraint_is(&inline_asm->outputs[index], "+r")) &&\n            !minic_riscv64_emit_sp_load64(file, operand_registers[index], index * 8U)) {''',
    "load =m address register",
)
codegen.write_text(text)

text = fixture.read_text()
text = replace_once(
    text,
    '''static int linux_target_constraint_shape(int value) {''',
    '''static void memory_output_store_like(int value, int *target) {\n    __asm__ __volatile__("sw %z1, %0" : "=m"(*target) : "rJ"(value) : "memory");\n}\n\nstatic int linux_target_constraint_shape(int value) {''',
    "memory output fixture",
)
fixture.write_text(text)

text = runner.read_text()
text = replace_once(
    text,
    '''grep -F 'amoadd.w t1, t3, (t0)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null''',
    '''grep -F 'amoadd.w t1, t3, (t0)' "$assembly" >/dev/null\ngrep -F '.type memory_output_store_like, @function' "$assembly" >/dev/null\ngrep -F 'sw t1, 0(t0)' "$assembly" >/dev/null\ngrep -F 'addi t3, zero, 7' "$assembly" >/dev/null''',
    "memory output assembly checks",
)
text = replace_once(
    text,
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64' ''',
    '''    'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=m,=r,+r inputs=r,rJ,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64' ''',
    "memory output PASS summary",
)
runner.write_text(text)
