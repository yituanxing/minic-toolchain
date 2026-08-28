#!/usr/bin/env python3
# Preserve empty volatile GNU asm as an explicit zero-length opaque Core effect.

from pathlib import Path

MARKER = "M89_EMPTY_VOLATILE_OPAQUE_ASM"
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M89 {name} anchor count={count}")
    return text.replace(old, new, 1)


def replace_in_region(text: str, begin: str, end: str, old: str, new: str, name: str) -> str:
    begin_index = text.find(begin)
    if begin_index < 0:
        raise SystemExit(f"M89 {name} region begin missing")
    end_index = text.find(end, begin_index + len(begin))
    if end_index < 0:
        raise SystemExit(f"M89 {name} region end missing")
    region = text[begin_index:end_index]
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"M89 {name} region anchor count={count}")
    region = region.replace(old, new, 1)
    return text[:begin_index] + region + text[end_index:]


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M89 core_ir.c already applied")
        return

    # A later direct call may coexist with an earlier empty opaque asm. The
    # callee table validation should require a valid volatile stored asm, but
    # zero target text is a legitimate opaque effect.
    text = replace_in_region(
        text,
        "bool minic_core_function_add_callee(",
        "/* M83_FIRST_CLASS_INDIRECT_CALL",
        '''        if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
            !inline_asm->is_volatile) {
''',
        '''        if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
''',
        "callee-inline-asm-validation",
    )

    old_guard = '''    if (function == NULL || template_text == NULL || template_length == 0U ||
        template_length == SIZE_MAX || inline_asm_id == NULL || !is_volatile ||
        function->inline_asm_count >= (size_t)UINT32_MAX) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.template_text = copy_name(template_text, template_length);
    if (stored.template_text == NULL || !grow_array((void **)&function->inline_asms,
'''
    new_guard = '''    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: an empty volatile asm is still an
       explicit compiler-side effect even though the target text is zero bytes.
       Keep it in the opaque-asm table rather than erasing it or strengthening it
       into a memory-clobber barrier. */
    if (function == NULL || template_text == NULL || template_length == SIZE_MAX ||
        inline_asm_id == NULL || !is_volatile ||
        function->inline_asm_count >= (size_t)UINT32_MAX) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    if (template_length == 0U) {
        stored.template_text = (char *)malloc(1U);
        if (stored.template_text != NULL) {
            stored.template_text[0] = '\\0';
        }
    } else {
        stored.template_text = copy_name(template_text, template_length);
    }
    if (stored.template_text == NULL || !grow_array((void **)&function->inline_asms,
'''
    text = replace_in_region(
        text,
        "bool minic_core_function_add_opaque_inline_asm(",
        "bool minic_core_function_append_call_arguments(",
        old_guard,
        new_guard,
        "opaque-store",
    )

    old_verify = '''        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
'''
    new_verify = '''        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->is_volatile;
'''
    text = replace_in_region(
        text,
        "case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:",
        "case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:",
        old_verify,
        new_verify,
        "opaque-instruction-verify",
    )
    IR_IMPL.write_text(text)
    print("M89 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M89 core_lower.c already applied")
        return

    anchor = '''    /* M59_EMPTY_SCALAR_INPUT_BARRIER: GNU barrier_data() is an empty
       volatile asm with one scalar register input and a memory clobber. The
'''
    if text.count(anchor) != 1:
        raise SystemExit(f"M89 lower anchor count={text.count(anchor)}")
    block = r'''    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: `asm volatile("")` carries a
       sequencing/volatile effect but intentionally emits no target text. Keep
       the effect explicitly in Core; do not invent a memory clobber. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U &&
        source->input_count == 0U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->clobber_count == 0U &&
        !source->has_memory_clobber) {
        if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                       source->template_text,
                                                       0U,
                                                       true,
                                                       false,
                                                       &inline_asm_id)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        instruction.span = statement->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.inline_asm_id = inline_asm_id;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

'''
    LOWER.write_text(text.replace(anchor, block + anchor, 1))
    print("M89 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M89 core_codegen.c already applied")
        return

    old = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
'''
    new = '''    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: opaque volatile asm may carry zero
       target bytes; emit_opaque_inline_asm naturally loops zero times. */
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
        return false;
    }
'''
    text = replace_in_region(
        text,
        "static bool core_opaque_inline_asm_supported(",
        "static bool core_register_output_inline_asm_supported(",
        old,
        new,
        "opaque-codegen-support",
    )
    CODEGEN.write_text(text)
    print("M89 core_codegen.c applied")


def main() -> int:
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
