#!/usr/bin/env python3
from pathlib import Path

core = Path('src/core/core_lower.c')
text = core.read_text()

anchor = '''static MinicCoreLowerStatus\nlower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {\n    size_t statement_index;\n'''
if text.count(anchor) != 1:
    raise SystemExit(f'lower_block implementation anchor mismatch: {text.count(anchor)}')

helper = r'''#define MINIC_CORE_SWITCH_LABEL_LIMIT 128U

typedef struct MinicCoreSwitchLabel {
    size_t source_index;
    const MinicStatement *statement;
    MinicCoreBlockId body_block;
    MinicCoreBlockId test_block;
} MinicCoreSwitchLabel;

static MinicCoreLowerStatus append_switch_integer_constant(MinicCoreLowerContext *context,
                                                           MinicSourceSpan span,
                                                           MinicType type,
                                                           int64_t value,
                                                           MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !minic_type_is_integer(type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus set_switch_conditional_branch(MinicCoreLowerContext *context,
                                                          MinicSourceSpan span,
                                                          MinicCoreValueId condition,
                                                          MinicCoreBlockId when_true,
                                                          MinicCoreBlockId when_false) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL ||
        condition >= context->function->value_count ||
        !minic_type_is_integer(context->function->values[condition].type) ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_switch_case_dispatch(MinicCoreLowerContext *context,
                                                       const MinicStatement *case_statement,
                                                       MinicType selector_type,
                                                       MinicCoreObjectId selector_object,
                                                       MinicCoreBlockId body_target,
                                                       MinicCoreBlockId next_target) {
    const MinicExpression *lower_expression;
    const MinicExpression *upper_expression;
    MinicCoreInstruction instruction;
    MinicCoreValueId bound;
    MinicCoreValueId comparison;
    MinicCoreValueId selector;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || case_statement == NULL ||
        case_statement->kind != MINIC_STATEMENT_CASE ||
        case_statement->expression == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_integer(selector_type) || body_target == MINIC_CORE_BLOCK_INVALID ||
        next_target == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    lower_expression =
        minic_c0_program_expression(context->body->program, case_statement->expression);
    if (lower_expression == NULL || lower_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(lower_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = reload_scalar_value(
        context, case_statement->span, selector_type, selector_object, &selector);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_switch_integer_constant(context,
                                            lower_expression->span,
                                            selector_type,
                                            lower_expression->value.integer_value,
                                            &bound);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_statement->target_expression == MINIC_EXPRESSION_INVALID) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, body_target, next_target);
    }

    upper_expression =
        minic_c0_program_expression(context->body->program, case_statement->target_expression);
    if (upper_expression == NULL || upper_expression->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(upper_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicCoreBlockId upper_test_block;

        if (!minic_core_function_add_block(context->function, &upper_test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, upper_test_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = upper_test_block;
        status = append_switch_integer_constant(context,
                                                upper_expression->span,
                                                selector_type,
                                                upper_expression->value.integer_value,
                                                &bound);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(
            context, case_statement->span, selector_type, selector_object, &selector);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = bound;
        instruction.value.binary.right = selector;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, body_target);
    }
}

static MinicCoreLowerStatus
lower_switch(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *body;
    const MinicExpression *selector_expression;
    MinicCoreSwitchLabel labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreObjectId selector_object;
    MinicCoreValueId selector_normalized;
    MinicCoreValueId selector_source;
    MinicCoreLowerStatus status;
    MinicType selector_type;
    size_t case_count;
    size_t default_label;
    size_t first_case_label;
    size_t label_count;
    size_t source_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_SWITCH ||
        statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID || context->target == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    selector_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    body = minic_c0_program_block(context->body->program, statement->then_block);
    if (selector_expression == NULL || body == NULL ||
        !minic_type_is_integer(selector_expression->type) ||
        !minic_target_info_integer_promotion_for_program(context->target,
                                                         context->body->program,
                                                         selector_expression->type,
                                                         &selector_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    case_count = 0U;
    default_label = SIZE_MAX;
    first_case_label = SIZE_MAX;
    label_count = 0U;
    for (source_index = 0U; source_index < body->statement_count; ++source_index) {
        const MinicStatement *source_statement;

        source_statement =
            minic_c0_program_statement(context->body->program, body->statements[source_index]);
        if (source_statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (source_statement->kind != MINIC_STATEMENT_CASE &&
            source_statement->kind != MINIC_STATEMENT_DEFAULT) {
            if (label_count == 0U) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            continue;
        }
        if (source_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
            source_statement->then_block != MINIC_BLOCK_INVALID ||
            source_statement->else_block != MINIC_BLOCK_INVALID ||
            label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        labels[label_count].source_index = source_index;
        labels[label_count].statement = source_statement;
        labels[label_count].body_block = MINIC_CORE_BLOCK_INVALID;
        labels[label_count].test_block = MINIC_CORE_BLOCK_INVALID;
        if (source_statement->kind == MINIC_STATEMENT_CASE) {
            if (source_statement->expression == MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (first_case_label == SIZE_MAX) {
                first_case_label = label_count;
            }
            case_count += 1U;
        } else {
            if (default_label != SIZE_MAX ||
                source_statement->expression != MINIC_EXPRESSION_INVALID ||
                source_statement->target_expression != MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            default_label = label_count;
        }
        label_count += 1U;
    }

    status = lower_expression(context, statement->expression, &selector_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(context,
                                       selector_expression->span,
                                       selector_type,
                                       selector_source,
                                       &selector_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(context,
                                selector_expression->span,
                                selector_type,
                                selector_normalized,
                                &selector_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (!minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (source_index = 0U; source_index < label_count; ++source_index) {
        if (!minic_core_function_add_block(context->function, &labels[source_index].body_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (labels[source_index].statement->kind == MINIC_STATEMENT_CASE &&
            !minic_core_function_add_block(context->function, &labels[source_index].test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;
    dispatch_target = first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;
    status = set_branch(context, context->block_id, statement->span, dispatch_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_count != 0U) {
        size_t label_index;

        for (label_index = 0U; label_index < label_count; ++label_index) {
            size_t next_label;
            MinicCoreBlockId next_target;

            if (labels[label_index].statement->kind != MINIC_STATEMENT_CASE) {
                continue;
            }
            next_target = default_target;
            for (next_label = label_index + 1U; next_label < label_count; ++next_label) {
                if (labels[next_label].statement->kind == MINIC_STATEMENT_CASE) {
                    next_target = labels[next_label].test_block;
                    break;
                }
            }
            context->block_id = labels[label_index].test_block;
            status = lower_switch_case_dispatch(context,
                                                labels[label_index].statement,
                                                selector_type,
                                                selector_object,
                                                labels[label_index].body_block,
                                                next_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }

    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
        size_t break_index;
        size_t segment_begin;
        size_t segment_end;
        size_t scan;
        bool segment_terminated;

        segment_begin = labels[source_index].source_index + 1U;
        segment_end = source_index + 1U < label_count ? labels[source_index + 1U].source_index
                                                     : body->statement_count;
        break_index = SIZE_MAX;
        for (scan = segment_begin; scan < segment_end; ++scan) {
            const MinicStatement *segment_statement;

            segment_statement =
                minic_c0_program_statement(context->body->program, body->statements[scan]);
            if (segment_statement == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (segment_statement->kind == MINIC_STATEMENT_BREAK) {
                if (break_index != SIZE_MAX || scan + 1U != segment_end ||
                    segment_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
                    segment_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                break_index = scan;
            }
        }

        context->block_id = labels[source_index].body_block;
        segment_terminated = false;
        segment = *body;
        segment.statements = body->statements + segment_begin;
        segment.statement_count =
            (break_index == SIZE_MAX ? segment_end : break_index) - segment_begin;
        segment.statement_capacity = segment.statement_count;
        if (segment.statement_count != 0U) {
            status = lower_block(context, &segment, &segment_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (segment_terminated) {
            continue;
        }
        if (break_index != SIZE_MAX) {
            fallthrough_target = exit_block;
        } else if (source_index + 1U < label_count) {
            fallthrough_target = labels[source_index + 1U].body_block;
        } else {
            fallthrough_target = exit_block;
        }
        status = set_branch(context, context->block_id, statement->span, fallthrough_target);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }

    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

'''
text = text.replace(anchor, helper + anchor, 1)

old = '''            case MINIC_STATEMENT_WHILE:\n                status = lower_while(context, statement, &statement_terminated);\n                break;\n            default:\n'''
new = '''            case MINIC_STATEMENT_WHILE:\n                status = lower_while(context, statement, &statement_terminated);\n                break;\n            case MINIC_STATEMENT_SWITCH:\n                status = lower_switch(context, statement, &statement_terminated);\n                break;\n            default:\n'''
if text.count(old) != 1:
    raise SystemExit(f'lower_block switch anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
core.write_text(text)

source = Path('tests/compiler/c0/core_switch_m27.c')
source.write_text(r'''int core_m27_switch_simple(int value) {
    switch (value) {
    case 1:
        return 10;
    case 2:
    case 3:
        return 20;
    default:
        return 30;
    }
}

int core_m27_switch_range(int value) {
    switch (value) {
    case 4 ... 7:
        return 1;
    default:
        return 0;
    }
}

int core_m27_switch_fallthrough(int value) {
    int result;

    result = 0;
    switch (value) {
    case 1:
        result = result + 1;
    case 2:
        result = result + 2;
        break;
    default:
        result = 9;
    }
    return result;
}

int core_m27_printk_get_level(const char *buffer) {
    if (buffer[0] == '\001' && buffer[1]) {
        switch (buffer[1]) {
        case '0' ... '7':
        case 'c':
            return buffer[1];
        }
    }
    return 0;
}
''')

runtime = Path('tests/compiler/c0/core_switch_m27_runtime.c')
runtime.write_text(r'''int core_m27_switch_simple(int value);
int core_m27_switch_range(int value);
int core_m27_switch_fallthrough(int value);
int core_m27_printk_get_level(const char *buffer);

int main(void) {
    const char level3[] = {'\001', '3', 0};
    const char levelc[] = {'\001', 'c', 0};
    const char plain[] = {'x', '3', 0};

    if (core_m27_switch_simple(1) != 10 || core_m27_switch_simple(3) != 20 ||
        core_m27_switch_simple(9) != 30) {
        return 1;
    }
    if (core_m27_switch_range(4) != 1 || core_m27_switch_range(7) != 1 ||
        core_m27_switch_range(8) != 0) {
        return 2;
    }
    if (core_m27_switch_fallthrough(1) != 3 || core_m27_switch_fallthrough(2) != 2 ||
        core_m27_switch_fallthrough(9) != 9) {
        return 3;
    }
    if (core_m27_printk_get_level(level3) != '3' ||
        core_m27_printk_get_level(levelc) != 'c' || core_m27_printk_get_level(plain) != 0) {
        return 4;
    }
    return 0;
}
''')

runner = Path('tests/compiler/c0/run-core-switch-m27.sh')
runner.write_text(r'''#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-switch-m27}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_switch_m27.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_switch_m27_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_switch_m27_runtime.c tests/compiler/c0/core_switch_m27.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-switch-m27'
''')

gate = Path('.github/scripts/compiler-c0-full-gate.sh')
gate_text = gate.read_text()
function_anchor = '''\nruntime_record_fam_prefix_focused() {\n'''
function_text = r'''
core_switch_m27_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-switch-m27" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-switch-m27.sh
}

'''
if gate_text.count(function_anchor) != 1:
    raise SystemExit(f'C0 focused function anchor mismatch: {gate_text.count(function_anchor)}')
gate_text = gate_text.replace(function_anchor, '\n' + function_text + 'runtime_record_fam_prefix_focused() {\n', 1)
start_anchor = 'start_gate core-integer-less-m26-focused core_integer_less_m26_focused\n'
if gate_text.count(start_anchor) != 1:
    raise SystemExit(f'C0 focused start anchor mismatch: {gate_text.count(start_anchor)}')
gate_text = gate_text.replace(
    start_anchor,
    start_anchor + 'start_gate core-switch-m27-focused core_switch_m27_focused\n',
    1,
)
gate.write_text(gate_text)
