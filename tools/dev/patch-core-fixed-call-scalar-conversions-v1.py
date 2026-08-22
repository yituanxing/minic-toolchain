from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
core_path = root / "src/core/core_lower.c"
gate_path = root / ".github/scripts/compiler-c0-full-gate.sh"
source_path = root / "tests/compiler/c0/core_fixed_call_scalar_conversions.c"
runtime_path = root / "tests/compiler/c0/core_fixed_call_scalar_conversions_runtime.c"
runner_path = root / "tests/compiler/c0/run-core-fixed-call-scalar-conversions.sh"

core = core_path.read_text()

scalar_bitcast_helper = r'''static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                   MinicSourceSpan span,
                                                   MinicType target_type,
                                                   MinicCoreValueId source_value,
                                                   MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    const MinicCoreValue *source;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    source = &context->function->values[source_value];
    if (minic_type_equal(source->type, target_type)) {
        *value_id = source_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (!core_scalar_bitcast_types(target_type, source->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    instruction.span = span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
core = replace_once(
    core,
    "static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n",
    scalar_bitcast_helper
    + "static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n",
    "insert scalar bitcast helper",
)

scalar_assignment_helper = r'''static MinicCoreLowerStatus lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId source_value;
    MinicCoreValueId zero_test;
    MinicCoreValueId truth_value;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || value_id == NULL || !core_memory_scalar_type(target_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        !minic_c0_assignment_compatible(context->body->program, target_type, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (source_value >= context->function->value_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (minic_type_is_pointer(target_type)) {
        if (!minic_type_is_pointer(expression->type) &&
            !minic_c0_expression_is_null_pointer_constant_v0(
                context->body->program, expression_id)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return append_scalar_bitcast(
            context, expression->span, target_type, source_value, value_id);
    }
    if (!minic_type_is_bool_integer(target_type) || !minic_type_is_pointer(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
    instruction.span = expression->span;
    instruction.type = minic_type_int();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.operand = source_value;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &zero_test)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    instruction.value.operand = zero_test;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &truth_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    return append_integer_conversion(
        context, expression->span, target_type, truth_value, value_id);
}

'''
core = replace_once(
    core,
    "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n",
    scalar_assignment_helper
    + "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n",
    "insert scalar assignment helper",
)

old_call_lower = r'''        status = lower_expression(
            context, expression->value.call.arguments[argument_index], &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[arguments[argument_index]].type,
                              callee->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
new_call_lower = r'''        status = lower_scalar_assignment_value(context,
                                               callee->parameter_types[argument_index],
                                               expression->value.call.arguments[argument_index],
                                               &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[arguments[argument_index]].type,
                              callee->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
'''
core = replace_once(core, old_call_lower, new_call_lower, "direct call scalar conversion")
core_path.write_text(core)

source_path.write_text(
    r'''static unsigned int core_m3_echo_unsigned(unsigned int value) {
    return value;
}

static const volatile void *core_m3_echo_qualified(const volatile void *value) {
    return value;
}

static const void *core_m3_echo_pointer(const void *value) {
    return value;
}

static _Bool core_m3_echo_bool(_Bool value) {
    return value;
}

static _Bool core_m3_kasan_check(const volatile void *address, unsigned int size) {
    return 1;
}

unsigned int core_m3_integer_conversion(void) {
    return core_m3_echo_unsigned(-1);
}

const volatile void *core_m3_pointer_qualification(const void *value) {
    return core_m3_echo_qualified(value);
}

const void *core_m3_null_pointer(void) {
    return core_m3_echo_pointer(0);
}

_Bool core_m3_pointer_bool(const void *value) {
    return core_m3_echo_bool(value);
}

unsigned long core_m3_read_word_at_a_time(const void *address) {
    core_m3_kasan_check(address, 1);
    return *(unsigned long *)address;
}
'''
)

runtime_path.write_text(
    r'''#include <stdio.h>

unsigned int core_m3_integer_conversion(void);
const volatile void *core_m3_pointer_qualification(const void *value);
const void *core_m3_null_pointer(void);
_Bool core_m3_pointer_bool(const void *value);
unsigned long core_m3_read_word_at_a_time(const void *address);

int main(void) {
    unsigned long word;
    int value;

    word = 0x1020304050607080UL;
    value = 17;
    (void)printf("%u %d %d %d %d %lu\n",
                 core_m3_integer_conversion(),
                 core_m3_pointer_qualification(&value) == &value,
                 core_m3_null_pointer() == 0,
                 core_m3_pointer_bool(&value),
                 core_m3_pointer_bool(0),
                 core_m3_read_word_at_a_time(&word));
    return 0;
}
'''
)

runner_path.write_text(
    r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-fixed-call-scalar-conversions}"
source_file="$root/tests/compiler/c0/core_fixed_call_scalar_conversions.c"
runtime_file="$root/tests/compiler/c0/core_fixed_call_scalar_conversions_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_fixed_call_scalar_conversions.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_fixed_call_scalar_conversions.i" \
    -o "$work/core_fixed_call_scalar_conversions-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_fixed_call_scalar_conversions.i" \
    -o "$work/core_fixed_call_scalar_conversions-core.s"

for symbol in \
    core_m3_integer_conversion \
    core_m3_pointer_qualification \
    core_m3_null_pointer \
    core_m3_pointer_bool \
    core_m3_read_word_at_a_time; do
    grep -q "^${symbol}:" "$work/core_fixed_call_scalar_conversions-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_fixed_call_scalar_conversions-core.s"
done

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_fixed_call_scalar_conversions-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-fixed-call-scalar-conversions'
'''
)

gate = gate_path.read_text()
gate_helper_anchor = r'''core_scalar_lvalue_bitcast_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-lvalue-bitcast" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-lvalue-bitcast.sh
}

'''
gate_helper_new = gate_helper_anchor + r'''core_fixed_call_scalar_conversions_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-fixed-call-scalar-conversions" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-fixed-call-scalar-conversions.sh
}

'''
gate = replace_once(gate, gate_helper_anchor, gate_helper_new, "gate helper")
gate = replace_once(
    gate,
    "start_gate core-scalar-lvalue-bitcast-focused core_scalar_lvalue_bitcast_focused\n",
    "start_gate core-scalar-lvalue-bitcast-focused core_scalar_lvalue_bitcast_focused\n"
    "start_gate core-fixed-call-scalar-conversions-focused core_fixed_call_scalar_conversions_focused\n",
    "gate invocation",
)
gate_path.write_text(gate)
