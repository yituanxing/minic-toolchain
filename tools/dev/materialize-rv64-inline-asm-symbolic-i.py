#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
codegen_path = root / 'src/target/riscv64/codegen_inline_asm.c'
text = codegen_path.read_text()

old_include = '#include "target/riscv64/codegen_internal.h"\n\n#include <inttypes.h>\n'
new_include = '#include "target/riscv64/codegen_internal.h"\n\n#include "frontend/const_eval.h"\n\n#include <inttypes.h>\n'
if text.count(old_include) != 1:
    raise SystemExit('inline asm include anchor missing')
text = text.replace(old_include, new_include, 1)

anchor = '''static bool constraint_is_immediate(const MinicInlineAsmOperand *operand) {
    return constraint_is(operand, "i") || constraint_is(operand, "I");
}

'''
helpers = anchor + '''static bool inline_asm_integer_immediate_value(const MinicC0Program *program,
                                               MinicExpressionId expression_id,
                                               int64_t *value) {
    const MinicTargetInfo *target;
    MinicConstValue constant;

    if (program == NULL || value == NULL) {
        return false;
    }
    target = minic_default_target_info();
    return target != NULL &&
           minic_const_eval_integer(program, target, expression_id, &constant) &&
           minic_const_value_as_int64(program, target, &constant, value);
}

static const MinicGlobalObject *inline_asm_symbolic_object_immediate(
    const MinicC0Program *program, MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicGlobalObjectId object_id;

    if (program == NULL) {
        return NULL;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return NULL;
    }
    addressed = minic_c0_program_expression(program, expression->value.unary.operand);
    if (addressed == NULL) {
        return NULL;
    }
    if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object_id = addressed->value.global_object_id;
    } else if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        const MinicGlobalObject *object;

        base = minic_c0_program_expression(program, addressed->value.subscript.base);
        index = minic_c0_program_expression(program, addressed->value.subscript.index);
        if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT || index == NULL ||
            index->kind != MINIC_EXPRESSION_INTEGER || !minic_type_is_integer(index->type) ||
            index->value.integer_value != 0) {
            return NULL;
        }
        object_id = base->value.global_object_id;
        object = minic_c0_program_global_object(program, object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return NULL;
        }
    } else {
        return NULL;
    }
    return object_id < program->global_object_count
               ? minic_c0_program_global_object(program, object_id)
               : NULL;
}

'''
if text.count(anchor) != 1:
    raise SystemExit('inline asm immediate helper anchor missing')
text = text.replace(anchor, helpers, 1)

old_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}
'''
new_validate = '''    if (inline_asm->is_goto) {
        if (!constraint_is(operand, "i")) {
            return false;
        }
    } else if (!constraint_is(operand, "r") && !constraint_is(operand, "I") &&
               !constraint_is(operand, "i")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return false;
    }
    if (!constraint_is(operand, "i") || inline_asm->is_goto) {
        return true;
    }
    if (minic_type_is_integer(expression->type)) {
        int64_t immediate_value;

        return inline_asm_integer_immediate_value(program, operand->expression, &immediate_value);
    }
    return inline_asm_symbolic_object_immediate(program, operand->expression) != NULL;
}
'''
if text.count(old_validate) != 1:
    raise SystemExit('inline asm input validation anchor missing')
text = text.replace(old_validate, new_validate, 1)

old_emit = '''    if (expression->kind == MINIC_EXPRESSION_INTEGER && minic_type_is_integer(expression->type)) {
        return fprintf(file, "%" PRId64, expression->value.integer_value) >= 0;
    }
    return fprintf(file,
                   "__minic_deferred_asm_immediate_%zu_%zu",
                   (size_t)inline_asm_id,
                   operand_index) >= 0;
}
'''
new_emit = '''    if (minic_type_is_integer(expression->type)) {
        int64_t immediate_value;

        if (inline_asm_integer_immediate_value(program, operand->expression, &immediate_value)) {
            return fprintf(file, "%" PRId64, immediate_value) >= 0;
        }
    }
    if (constraint_is(operand, "i") && minic_type_is_pointer(expression->type)) {
        const MinicGlobalObject *object;

        object = inline_asm_symbolic_object_immediate(program, operand->expression);
        if (object != NULL && object->name != NULL && object->name_length != 0U) {
            return fprintf(file, "%.*s", (int)object->name_length, object->name) >= 0;
        }
    }
    return fprintf(file,
                   "__minic_deferred_asm_immediate_%zu_%zu",
                   (size_t)inline_asm_id,
                   operand_index) >= 0;
}
'''
if text.count(old_emit) != 1:
    raise SystemExit('inline asm immediate emission anchor missing')
text = text.replace(old_emit, new_emit, 1)
codegen_path.write_text(text)

source_path = root / 'tests/compiler/c0/gnu_inline_asm_operands.c'
source = source_path.read_text()
insert = '''\nstatic void linux_bug_immediate_shape(void) {\n    __asm__ __volatile__(\n        "1:\\n\\t"\n        "ebreak\\n"\n        ".pushsection __bug_table,\\\"aw\\\"\\n\\t"\n        "2:\\n\\t"\n        ".word 1b - .\\n\\t"\n        ".word %0 - .\\n\\t"\n        ".half %1\\n\\t"\n        ".half %2\\n\\t"\n        ".org 2b + %3\\n\\t"\n        ".popsection"\n        :\n        : "i"("init/main.c"),\n          "i"(1262),\n          "i"((1 << 0) | ((1 << 3) | (9 << 8))),\n          "i"(sizeof(AtomicLike)));\n}\n'''
marker = '\nint main(void) {'
if source.count(marker) != 1:
    raise SystemExit('inline asm focused source marker missing')
source_path.write_text(source.replace(marker, insert + marker, 1))

run_path = root / 'tests/compiler/c0/run-gnu-inline-asm-operands.sh'
run = run_path.read_text()
needle = '''grep -F 'add t0, t1, t4' "$assembly" >/dev/null\n'''
extra = needle + '''grep -E '\\.word \\.Lminic_string_[0-9]+ - \\.' "$assembly" >/dev/null\ngrep -F '.half 1262' "$assembly" >/dev/null\ngrep -F '.half 2313' "$assembly" >/dev/null\ngrep -F '.org 2b + 4' "$assembly" >/dev/null\n'''
if run.count(needle) != 1:
    raise SystemExit('inline asm focused assertion anchor missing')
run = run.replace(needle, extra, 1)
old_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'"
new_msg = "'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r,+r inputs=r,I,i clobber=memory,t3 symbolic-i=global-string const-i=typed-consteval staging=stack target=RV64'"
if run.count(old_msg) != 1:
    raise SystemExit('inline asm focused message anchor missing')
run_path.write_text(run.replace(old_msg, new_msg, 1))
