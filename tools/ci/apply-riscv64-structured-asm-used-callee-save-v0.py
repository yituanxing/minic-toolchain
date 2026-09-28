#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()

struct_old = "    size_t structured_asm_callee_saved_offset;\n    size_t varargs_offset;"
struct_new = "    size_t structured_asm_callee_saved_offset;\n    uint16_t structured_asm_callee_saved_mask;\n    size_t varargs_offset;"
if struct_old not in text:
    raise SystemExit("frame structured asm field anchor not found")
text = text.replace(struct_old, struct_new, 1)

frame_marker = "static bool core_frame_initialize(const MinicC0Program *program,"
if frame_marker not in text:
    raise SystemExit("core_frame_initialize marker not found")

helper = r'''/* RUNTIME_STRUCTURED_ASM_USED_CALLEE_SAVE_V0: M172 originally saved the
   complete s0-s11 bank whenever a function contained structured inline asm.
   Keep the allocator and ABI semantics unchanged, but pre-run the deterministic
   asm allocator while sizing the frame and preserve only callee-saved registers
   that are actually selected as operands or explicitly named as clobbers. */
static bool core_structured_inline_asm_allocate(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    const char **operand_registers,
    bool *memory_operand,
    const char **scratch_register);

static bool core_asm_callee_saved_index_for_mask(const char *name, size_t *result) {
    size_t index;
    if (name == NULL || result == NULL) {
        return false;
    }
    for (index = 0U; index < CORE_ASM_CALLEE_SAVED_COUNT; ++index) {
        if (strcmp(name, core_asm_callee_saved_registers[index]) == 0) {
            *result = index;
            return true;
        }
    }
    return false;
}

static size_t core_asm_callee_saved_mask_count(uint16_t mask) {
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < CORE_ASM_CALLEE_SAVED_COUNT; ++index) {
        if ((mask & (uint16_t)(1U << index)) != 0U) {
            ++count;
        }
    }
    return count;
}

static bool core_function_structured_asm_callee_saved_mask(
    const MinicCoreFunction *function, uint16_t *result) {
    uint16_t mask = 0U;
    size_t instruction_index;

    if (function == NULL || result == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        const MinicCoreInlineAsm *inline_asm;
        const char *operand_registers[10] = {NULL};
        bool memory_operand[10] = {false};
        const char *scratch_register = NULL;
        size_t index;

        if (instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
            continue;
        }
        if (instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            !core_structured_inline_asm_allocate(function,
                                                 instruction,
                                                 operand_registers,
                                                 memory_operand,
                                                 &scratch_register)) {
            return false;
        }
        (void)memory_operand;
        (void)scratch_register;
        for (index = 0U; index < 10U; ++index) {
            size_t saved_index;
            if (operand_registers[index] != NULL &&
                core_asm_callee_saved_index_for_mask(operand_registers[index], &saved_index)) {
                mask |= (uint16_t)(1U << saved_index);
            }
        }
        inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
        for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
            const MinicCoreInlineAsmRegisterClobber *clobber =
                &inline_asm->register_clobbers[index];
            size_t saved_index;
            if (clobber->name != NULL &&
                core_asm_callee_saved_index_for_mask(clobber->name, &saved_index)) {
                mask |= (uint16_t)(1U << saved_index);
            }
        }
    }
    *result = mask;
    return true;
}

'''
text = text.replace(frame_marker, helper + frame_marker, 1)

old_frame = r'''    frame->preserves_structured_asm_callee_saved =
        core_function_uses_structured_inline_asm(function);
    /*
     * Structured inline asm may explicitly bind/clobber s0. Dynamic-stack
     * functions reserve s0 as the stable fixed-frame base, so keep the mixed
     * case fail-closed until the asm allocator can reserve a frame register.
     */
    if (frame->has_dynamic_stack_alloc && frame->preserves_structured_asm_callee_saved) {
        return false;
    }
    frame->structured_asm_callee_saved_offset = 0U;
    if (frame->preserves_structured_asm_callee_saved) {
        size_t saved_bytes = CORE_ASM_CALLEE_SAVED_COUNT * 8U;
        if (!align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }
'''
new_frame = r'''    frame->preserves_structured_asm_callee_saved =
        core_function_uses_structured_inline_asm(function);
    /*
     * Structured inline asm may explicitly bind/clobber s0. Dynamic-stack
     * functions reserve s0 as the stable fixed-frame base, so keep the mixed
     * case fail-closed until the asm allocator can reserve a frame register.
     */
    if (frame->has_dynamic_stack_alloc && frame->preserves_structured_asm_callee_saved) {
        return false;
    }
    frame->structured_asm_callee_saved_mask = 0U;
    if (frame->preserves_structured_asm_callee_saved &&
        !core_function_structured_asm_callee_saved_mask(
            function, &frame->structured_asm_callee_saved_mask)) {
        return false;
    }
    frame->structured_asm_callee_saved_offset = 0U;
    if (frame->structured_asm_callee_saved_mask != 0U) {
        size_t saved_count =
            core_asm_callee_saved_mask_count(frame->structured_asm_callee_saved_mask);
        size_t saved_bytes = saved_count * 8U;
        if (!align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }
'''
if old_frame not in text:
    raise SystemExit("structured asm frame sizing block not found")
text = text.replace(old_frame, new_frame, 1)

loop_head = r'''        size_t saved_index;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset = frame.structured_asm_callee_saved_offset + saved_index * 8U;'''
loop_new = r'''        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.structured_asm_callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;'''
loop_count = text.count(loop_head)
if loop_count != 2:
    raise SystemExit(f"expected two structured asm save/restore loops, found {loop_count}")
text = text.replace(loop_head, loop_new)

p.write_text(text)
print("MINIC_RISCV64_STRUCTURED_ASM_USED_CALLEE_SAVE_V0=APPLIED")
