#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M172 final-four {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


def replace_region(start_marker: str, end_marker: str, old: str, new: str, label: str) -> None:
    global text
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    region = text[start:end]
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"M172 final-four {label}: expected 1 regional match, got {count}")
    text = text[:start] + region.replace(old, new, 1) + text[end:]


# ---------------------------------------------------------------------------
# Owner A: structured asm may exhaust all caller-saved registers.  Preserve
# the RV64 callee-saved bank in the function frame and allow the operand
# allocator to fall back to it.  Core itself remains memory-form/O0 and never
# uses these as long-lived value registers.
# ---------------------------------------------------------------------------
replace_once(
    '''static const char *const minic_core_rv64_argument_registers[8] = {
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
};
''',
    '''static const char *const minic_core_rv64_argument_registers[8] = {
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
};

/* M172_STRUCTURED_ASM_CALLEE_SAVED: basic-v0 conservatively preserves the
   complete RV64 callee-saved bank for functions containing structured asm.
   This lets asm operands fall back to s-registers when every caller-saved
   candidate is clobbered without making Core itself a register allocator. */
static const char *const core_asm_callee_saved_registers[] = {
    "s0", "s1", "s2", "s3", "s4", "s5",
    "s6", "s7", "s8", "s9", "s10", "s11",
};
#define CORE_ASM_CALLEE_SAVED_COUNT \
    (sizeof(core_asm_callee_saved_registers) / sizeof(core_asm_callee_saved_registers[0]))
''',
    "callee-saved-table",
)

replace_once(
    '''    size_t entry_sp_offset;
    size_t stack_alignment;
    size_t varargs_offset;
''',
    '''    size_t entry_sp_offset;
    size_t stack_alignment;
    size_t structured_asm_callee_saved_offset;
    size_t varargs_offset;
''',
    "frame-callee-saved-offset",
)
replace_once(
    '''    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool has_variadic_argument_address;
''',
    '''    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool preserves_structured_asm_callee_saved;
    bool has_variadic_argument_address;
''',
    "frame-callee-saved-flag",
)

anchor = '''static bool core_function_uses_variadic_argument_address(
    const MinicCoreFunction *function) {
'''
helper = '''static bool core_function_uses_structured_inline_asm(
    const MinicCoreFunction *function) {
    size_t instruction_index;

    if (function == NULL) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        if (function->instructions[instruction_index].kind ==
            MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
            return true;
        }
    }
    return false;
}

''' + anchor
replace_once(anchor, helper, "structured-asm-function-owner")

replace_once(
    '''    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
''',
    '''    frame->preserves_structured_asm_callee_saved =
        core_function_uses_structured_inline_asm(function);
    frame->structured_asm_callee_saved_offset = 0U;
    if (frame->preserves_structured_asm_callee_saved) {
        size_t saved_bytes = CORE_ASM_CALLEE_SAVED_COUNT * 8U;
        if (!align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }

    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
''',
    "frame-callee-saved-storage",
)

replace_once(
    '''typedef struct MinicCoreRiscv64AsmRegisterCandidate {
    const char *name;
} MinicCoreRiscv64AsmRegisterCandidate;
''',
    '''typedef struct MinicCoreRiscv64AsmRegisterCandidate {
    const char *name;
} MinicCoreRiscv64AsmRegisterCandidate;
''',
    "allocator-type-anchor",
)

caller_helper = '''static bool core_asm_register_is_caller_saved(const char *name) {
    size_t index;
    for (index = 0U;
         index < sizeof(core_asm_caller_saved_registers) /
                     sizeof(core_asm_caller_saved_registers[0]);
         ++index) {
        if (core_asm_register_name_equal(name, core_asm_caller_saved_registers[index].name)) {
            return true;
        }
    }
    return false;
}
'''
replace_once(
    caller_helper,
    caller_helper + '''

static bool core_asm_register_is_callee_saved(const char *name) {
    size_t index;
    for (index = 0U; index < CORE_ASM_CALLEE_SAVED_COUNT; ++index) {
        if (core_asm_register_name_equal(name, core_asm_callee_saved_registers[index])) {
            return true;
        }
    }
    return false;
}
''',
    "callee-saved-recognizer",
)

replace_once(
    '''    static const char *const output_preferences[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
    static const char *const memory_preferences[] = {
        "t2", "t6", "t5", "t4", "t3", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
    static const char *const input_preferences[] = {
        "t3", "t4", "t5", "t6", "t2", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    };
''',
    '''    static const char *const output_preferences[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const memory_preferences[] = {
        "t2", "t6", "t5", "t4", "t3", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
    static const char *const input_preferences[] = {
        "t3", "t4", "t5", "t6", "t2", "t1", "t0",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s0",
    };
''',
    "allocator-preferences",
)

replace_once(
    '''    /* M126A remains caller-saved-only. Explicit callee-saved clobbers need
       function-frame preservation and are deliberately deferred to M126B. */
    for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
         ++clobber_index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[clobber_index];
        if (clobber->name == NULL || !core_asm_register_is_caller_saved(clobber->name)) {
            return false;
        }
    }
''',
    '''    /* M172: explicit callee-saved clobbers are valid because the function
       frame now preserves the complete callee-saved bank. Unknown architectural
       register names still fail closed. */
    for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
         ++clobber_index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[clobber_index];
        if (clobber->name == NULL ||
            (!core_asm_register_is_caller_saved(clobber->name) &&
             !core_asm_register_is_callee_saved(clobber->name))) {
            return false;
        }
    }
''',
    "callee-saved-clobber-admissibility",
)

# Save after the dynamically-aligned body SP has been established, and restore
# before M171 restores entry SP.
replace_once(
    '''    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_emit_sp_store64(file, "a0", frame.hidden_result_pointer_offset)) {
        return false;
    }
    if (frame.has_variadic_argument_address) {
''',
    '''    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_emit_sp_store64(file, "a0", frame.hidden_result_pointer_offset)) {
        return false;
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset = frame.structured_asm_callee_saved_offset + saved_index * 8U;
            if (!minic_riscv64_emit_sp_store64(
                    file, core_asm_callee_saved_registers[saved_index], saved_offset)) {
                return false;
            }
        }
    }
    if (frame.has_variadic_argument_address) {
''',
    "prologue-preserve-callee-saved",
)

replace_once(
    '''    if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\\n") < 0) {
            return false;
        }
''',
    '''    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        for (saved_index = 0U; saved_index < CORE_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset = frame.structured_asm_callee_saved_offset + saved_index * 8U;
            if (!minic_riscv64_emit_sp_load64(
                    file, core_asm_callee_saved_registers[saved_index], saved_offset)) {
                return false;
            }
        }
    }
    if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\\n") < 0) {
            return false;
        }
''',
    "epilogue-restore-callee-saved",
)

# ---------------------------------------------------------------------------
# Owner B: M163 already reserves the maximum outgoing stack area. Extend direct
# record arguments so ABI aggregate chunks (and indirect aggregate pointers)
# may use that area instead of requiring all chunks to remain in a0-a7.
# ---------------------------------------------------------------------------
replace_region(
    "static bool core_direct_call_supported(",
    "static bool core_indirect_call_supported(",
    '''            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.stack_slot_count != 0U ||
                    location.integer_register_count != location.value.slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.value.slot_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_count != 1U ||
                    location.integer_register_begin >= 8U) {
                    return false;
                }
''',
    '''            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                    location.value.slot_count !=
                        location.integer_register_count + location.stack_slot_count ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return false;
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.value.slot_count != 1U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return false;
                }
''',
    "direct-record-stack-preflight",
)

replace_region(
    "static bool emit_call(FILE *file,",
    "static bool emit_indirect_call(FILE *file,",
    '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count != 1U || location.stack_slot_count != 0U ||
                    location.integer_register_begin >= 8U ||
                    !emit_sp_address(file,
                                     minic_core_rv64_argument_registers[
                                         location.integer_register_begin],
                                     object_offset)) {
                    return false;
                }
                continue;
            }
''',
    '''            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
                    if (location.integer_register_begin >= 8U ||
                        !emit_sp_address(file,
                                         minic_core_rv64_argument_registers[
                                             location.integer_register_begin],
                                         object_offset)) {
                        return false;
                    }
                } else if (location.integer_register_count == 0U &&
                           location.stack_slot_count == 1U) {
                    size_t outgoing_offset;
                    if (location.stack_slot_begin > SIZE_MAX / 8U ||
                        !emit_sp_address(file, "t0", object_offset)) {
                        return false;
                    }
                    outgoing_offset = location.stack_slot_begin * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                } else {
                    return false;
                }
                continue;
            }
''',
    "direct-indirect-record-stack-emitter",
)

replace_region(
    "static bool emit_call(FILE *file,",
    "static bool emit_indirect_call(FILE *file,",
    '''            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;
                size_t register_index = location.integer_register_begin + chunk_index;

                if (chunk_offset >= location.value.storage_size || register_index >= 8U ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (!emit_sp_load_chunk(file,
                                        minic_core_rv64_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            }
''',
    '''            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;

                if (chunk_offset >= location.value.storage_size ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (chunk_index < location.integer_register_count) {
                    size_t register_index = location.integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        !emit_sp_load_chunk(file,
                                            minic_core_rv64_argument_registers[register_index],
                                            object_offset + chunk_offset,
                                            chunk_size)) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location.integer_register_count;
                    size_t stack_slot;
                    size_t outgoing_offset;
                    if (stack_chunk >= location.stack_slot_count ||
                        location.stack_slot_begin > SIZE_MAX - stack_chunk) {
                        return false;
                    }
                    stack_slot = location.stack_slot_begin + stack_chunk;
                    if (stack_slot > SIZE_MAX / 8U ||
                        !emit_sp_load_chunk(
                            file, "t0", object_offset + chunk_offset, chunk_size)) {
                        return false;
                    }
                    outgoing_offset = stack_slot * 8U;
                    if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                        return false;
                    }
                }
            }
''',
    "direct-aggregate-stack-emitter",
)

PATH.write_text(text)
print("M172_FINAL_FOUR_APPLIED")
