#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M166 fixed asm phase alias {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


allocator_anchor = '''static bool core_structured_inline_asm_allocate(\n'''
helper = r'''/* M166_RV64_FIXED_ASM_PHASE_ALIAS: two distinct fixed-register operands may
   intentionally share one architectural register when one is a write-only
   output and the other is a scalar input. Their lifetimes are disjoint:
   input before asm, output after asm. Early-clobber outputs cannot alias. */
static bool core_structured_fixed_phase_alias_safe(
    const MinicCoreInstruction *instruction,
    size_t current_binding_index,
    const char *const *operand_registers,
    const char *register_name) {
    const MinicCoreStructuredInlineAsmOperand *current;
    size_t alias_count;
    size_t prior_index;

    if (instruction == NULL || operand_registers == NULL || register_name == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        current_binding_index >= instruction->value.structured_inline_asm.operand_count) {
        return false;
    }
    current = &instruction->value.structured_inline_asm.operands[current_binding_index];
    if (!current->has_fixed_register_binding) {
        return false;
    }
    alias_count = 0U;
    for (prior_index = 0U; prior_index < current_binding_index; ++prior_index) {
        const MinicCoreStructuredInlineAsmOperand *prior =
            &instruction->value.structured_inline_asm.operands[prior_index];
        bool current_is_input;
        bool current_is_output;
        bool prior_is_input;
        bool prior_is_output;
        const MinicCoreStructuredInlineAsmOperand *output;

        if (!prior->has_fixed_register_binding || prior->operand_index > 9U ||
            !core_asm_register_name_equal(
                operand_registers[prior->operand_index], register_name)) {
            continue;
        }
        alias_count += 1U;
        if (alias_count != 1U) {
            return false;
        }
        current_is_input =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        current_is_output =
            current->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        prior_is_input = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        prior_is_output = prior->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (!((current_is_input && prior_is_output) ||
              (current_is_output && prior_is_input))) {
            return false;
        }
        output = current_is_output ? current : prior;
        if (output->early_clobber) {
            return false;
        }
    }
    return alias_count == 1U;
}

static bool core_structured_inline_asm_allocate(
'''
replace_once(allocator_anchor, helper, "helper")

replace_once(
    '''        if (fixed_binding == NULL || !fixed_binding->is_local ||\n            fixed_binding->register_name == NULL || fixed_binding->register_name_length == 0U ||\n            core_inline_asm_clobbers_register(inline_asm, fixed_binding->register_name) ||\n            core_asm_register_in_use(operand_registers, 10U, fixed_binding->register_name)) {\n            return false;\n        }\n        operand_registers[binding->operand_index] = fixed_binding->register_name;\n''',
    '''        if (fixed_binding == NULL || !fixed_binding->is_local ||\n            fixed_binding->register_name == NULL || fixed_binding->register_name_length == 0U ||\n            core_inline_asm_clobbers_register(inline_asm, fixed_binding->register_name)) {\n            return false;\n        }\n        if (core_asm_register_in_use(operand_registers, 10U, fixed_binding->register_name) &&\n            !core_structured_fixed_phase_alias_safe(\n                instruction, binding_index, operand_registers, fixed_binding->register_name)) {\n            return false;\n        }\n        operand_registers[binding->operand_index] = fixed_binding->register_name;\n''',
    "fixed-reservation",
)

PATH.write_text(text)
print("M166_FIXED_ASM_PHASE_ALIAS_APPLIED")
