#!/usr/bin/env python3
from pathlib import Path

path = Path('src/target/riscv64/codegen_statement.c')
text = path.read_text()
old = '''    return minic_riscv64_emit_expression(file, program, function, statement->expression) &&
           fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") >= 0 &&
           minic_riscv64_emit_lvalue_address(
               file, program, function, statement->target_expression) &&
           fprintf(file, "  ld t0, 0(sp)\\n  addi sp, sp, 16\\n") >= 0 &&
           (!minic_type_is_integer(target->type) ||
            minic_riscv64_emit_integer_conversion(file, target->type, "t0")) &&
           minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0");
'''
new = '''    fprintf(stderr,
            "ASSIGN_DEBUG function=%s target=%zu kind=%d base=%d ptr=%u value=%zu kind=%d base=%d ptr=%u\\n",
            function->name,
            (size_t)statement->target_expression,
            (int)target->kind,
            (int)target->type.base_kind,
            target->type.pointer_depth,
            (size_t)statement->expression,
            (int)value->kind,
            (int)value->type.base_kind,
            value->type.pointer_depth);
    if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
        fprintf(stderr, "ASSIGN_FAIL stage=value function=%s\\n", function->name);
        return false;
    }
    if (fprintf(file, "  addi sp, sp, -16\\n  sd a0, 0(sp)\\n") < 0) {
        fprintf(stderr, "ASSIGN_FAIL stage=save function=%s\\n", function->name);
        return false;
    }
    if (!minic_riscv64_emit_lvalue_address(
            file, program, function, statement->target_expression)) {
        fprintf(stderr, "ASSIGN_FAIL stage=target-address function=%s\\n", function->name);
        return false;
    }
    if (fprintf(file, "  ld t0, 0(sp)\\n  addi sp, sp, 16\\n") < 0) {
        fprintf(stderr, "ASSIGN_FAIL stage=restore function=%s\\n", function->name);
        return false;
    }
    if (minic_type_is_integer(target->type) &&
        !minic_riscv64_emit_integer_conversion(file, target->type, "t0")) {
        fprintf(stderr, "ASSIGN_FAIL stage=conversion function=%s\\n", function->name);
        return false;
    }
    if (!minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0")) {
        fprintf(stderr, "ASSIGN_FAIL stage=store function=%s\\n", function->name);
        return false;
    }
    return true;
'''
if text.count(old) != 1:
    raise SystemExit('scalar assignment codegen anchor missing')
path.write_text(text.replace(old, new, 1))
