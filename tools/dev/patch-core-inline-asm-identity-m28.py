#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M28 {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


core_path = Path("src/core/core_lower.c")
core = core_path.read_text()
old_guard = '''    if (!source->is_volatile || source->is_goto || source->template_text == NULL ||
        source->template_length == 0U || source->output_count != 0U || source->input_count != 0U ||
        source->label_count != 0U || source->register_clobber_count != 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
identity_guard = '''    if (!source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->outputs != NULL && source->output_count == 1U &&
        source->input_count == 0U && source->label_count == 0U && source->clobber_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber) {
        const MinicInlineAsmOperand *output;
        const MinicExpression *output_expression;
        const MinicLocal *local;

        output = &source->outputs[0];
        output_expression =
            minic_c0_program_expression(context->body->program, output->expression);
        if (output->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE &&
            output->constraint_text != NULL && output->constraint_length == 3U &&
            memcmp(output->constraint_text, "+rm", 3U) == 0 && output_expression != NULL &&
            output_expression->kind == MINIC_EXPRESSION_LOCAL &&
            output_expression->value_category == MINIC_VALUE_LVALUE &&
            core_memory_scalar_type(output_expression->type) &&
            !minic_type_is_const(output_expression->type) &&
            !minic_type_is_volatile(output_expression->type)) {
            local = minic_c0_program_local(context->body->program, output_expression->value.local_id);
            if (local == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!local->is_array && !local->is_register_storage &&
                minic_type_equal(local->type, output_expression->type) &&
                !minic_type_is_const(local->type) && !minic_type_is_volatile(local->type)) {
                return MINIC_CORE_LOWER_OK;
            }
        }
    }

''' + old_guard
core = replace_once(core, old_guard, identity_guard, "Core inline-asm guard")
core_path.write_text(core)

gate_path = Path(".github/scripts/compiler-c0-full-gate.sh")
gate = gate_path.read_text()
old_function_anchor = '''core_switch_m27_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-switch-m27" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-switch-m27.sh
}

runtime_record_fam_prefix_focused() {
'''
new_function_anchor = '''core_switch_m27_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-switch-m27" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-switch-m27.sh
}

core_inline_asm_identity_m28_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-inline-asm-identity-m28" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-inline-asm-identity-m28.sh
}

runtime_record_fam_prefix_focused() {
'''
gate = replace_once(gate, old_function_anchor, new_function_anchor, "gate function")
old_start = '''start_gate core-switch-m27-focused core_switch_m27_focused
start_gate core-integer-foundation-m26b-focused core_integer_foundation_m26b_focused
'''
new_start = '''start_gate core-switch-m27-focused core_switch_m27_focused
start_gate core-inline-asm-identity-m28-focused core_inline_asm_identity_m28_focused
start_gate core-integer-foundation-m26b-focused core_integer_foundation_m26b_focused
'''
gate = replace_once(gate, old_start, new_start, "gate start")
gate_path.write_text(gate)

print("M28_PATCH_APPLIED")
