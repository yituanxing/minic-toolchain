#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}")
    path.write_text(text.replace(old, new, 1))


codegen = Path("src/target/riscv64/codegen_expression.c")
replace_once(
    codegen,
    '''    case MINIC_EXPRESSION_DISCARD:\n        return minic_type_is_void(expression->type) &&\n               minic_riscv64_emit_expression(\n                   file, program, function, function_layout, expression->value.unary.operand);\n''',
    '''    case MINIC_EXPRESSION_DISCARD: {\n        const MinicExpression *operand;\n        MinicExpressionId operand_id;\n\n        if (!minic_type_is_void(expression->type)) {\n            return false;\n        }\n        operand_id = expression->value.unary.operand;\n        operand = minic_c0_program_expression(program, operand_id);\n        if (operand == NULL) {\n            return false;\n        }\n        if (minic_type_is_record(operand->type) &&\n            minic_c0_record_value_is_address_backed(program, operand_id)) {\n            return minic_riscv64_emit_address_backed_record_value(\n                file, program, function, function_layout, operand_id);\n        }\n        return minic_riscv64_emit_expression(\n            file, program, function, function_layout, operand_id);\n    }\n''',
)

fixture = Path("tests/compiler/c0/gnu_statement_record_value.c")
fixture.write_text(
    fixture.read_text()
    + '''\ntypedef struct discarded_record {\n    long value;\n} discarded_record_t;\n\ntypedef struct discarded_empty {\n} discarded_empty_t;\n\ntypedef struct discarded_holder {\n    discarded_empty_t cookie;\n} discarded_holder_t;\n\nstatic discarded_record_t *discard_record_source(discarded_record_t *value)\n{\n    return value;\n}\n\nvoid discard_record_lvalue(discarded_record_t *value)\n{\n    (void)(*discard_record_source(value));\n}\n\nvoid discard_zero_record_member(discarded_holder_t *holder)\n{\n    (void)(holder->cookie);\n}\n'''
)

runner = Path("tests/compiler/c0/run-gnu-statement-record-value.sh")
replace_once(
    runner,
    '''grep -F '  call try_lock' "$assembly" >/dev/null\n''',
    '''grep -F '  call try_lock' "$assembly" >/dev/null\ngrep -F '.type discard_record_lvalue, @function' "$assembly" >/dev/null\ngrep -F '  call discard_record_source' "$assembly" >/dev/null\ngrep -F '.type discard_zero_record_member, @function' "$assembly" >/dev/null\n''',
)
replace_once(
    runner,
    '''    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue address-backed=preserved call-rvalue=8+16-register+24-indirect auto-type=1 hidden-result=1 lvalue=unchanged'\n''',
    '''    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue address-backed=preserved discard=record-lvalue+zero-member call-rvalue=8+16-register+24-indirect auto-type=1 hidden-result=1 lvalue=unchanged'\n''',
)
