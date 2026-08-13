#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'src/target/riscv64/codegen_statement.c'
text = path.read_text()
old = '''    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_assignment_compatible(program, target->type, statement->expression)) {
        return false;
    }
    return minic_riscv64_emit_expression(file, program, function, statement->expression) &&
'''
new = '''    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_assignment_compatible(program, target->type, statement->expression)) {
        return false;
    }
    if (minic_type_is_record(target->type)) {
        return minic_type_is_record(value->type) && target->type.record_id == value->type.record_id &&
               minic_c0_record_value_is_copy_source(program, statement->expression) &&
               minic_riscv64_emit_record_copy_value(file,
                                                     program,
                                                     function,
                                                     statement->target_expression,
                                                     statement->expression,
                                                     false);
    }
    return minic_riscv64_emit_expression(file, program, function, statement->expression) &&
'''
if text.count(old) != 1:
    raise SystemExit('statement assignment lowering anchor missing')
path.write_text(text.replace(old, new, 1))

source_path = root / 'tests/compiler/c0/record_assignment_expression.c'
source = source_path.read_text()
insert = '''\nstruct SemaphoreLike {\n    unsigned int count;\n    struct { void *next; void *prev; } wait;\n};\n\nstatic void initialize_through_pointer(struct SemaphoreLike *sem, int value) {\n    *sem = (struct SemaphoreLike){ .count = (unsigned int)value, .wait = { &sem->wait, &sem->wait } };\n}\n'''
marker = '\nint main(void) {'
if source.count(marker) != 1:
    raise SystemExit('record assignment focused source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-record-assignment-expressions.sh'
run = run_path.read_text()
needle = '''grep -F 'main:' "$work/record_assignment_expression.s" >/dev/null\n'''
extra = needle + '''grep -F 'initialize_through_pointer:' "$work/record_assignment_expression.s" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('record assignment focused assertion marker missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/record_assignment_expression whole-object-copy=1 comma-discard=1 alias-safe-temp=1'"
new_msg = "'PASS compiler/c0/record_assignment_expression whole-object-copy=1 pointer-target-compound-literal=1 comma-discard=1 alias-safe-temp=1'"
if run.count(old_msg) != 1:
    raise SystemExit('record assignment focused message marker missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
