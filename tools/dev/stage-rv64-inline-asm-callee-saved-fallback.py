#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


path = Path("src/target/riscv64/codegen_inline_asm.c")
text = path.read_text()

text = replace_once(
    text,
    '''#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U\n\nstatic const char *const minic_riscv64_inline_asm_registers[] = {\n    "t0", "t1", "t3", "t4", "t5", "t6"};\n''',
    '''#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U\n\ntypedef struct MinicRiscv64InlineAsmRegisterCandidate {\n    const char *name;\n    bool is_callee_saved;\n} MinicRiscv64InlineAsmRegisterCandidate;\n\nstatic const MinicRiscv64InlineAsmRegisterCandidate minic_riscv64_inline_asm_registers[] = {\n    {"t0", false},\n    {"t1", false},\n    {"t3", false},\n    {"t4", false},\n    {"t5", false},\n    {"t6", false},\n    {"s1", true},\n    {"s2", true},\n    {"s3", true},\n    {"s4", true},\n    {"s5", true},\n    {"s6", true},\n};\n\n#define MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT \\\n    (sizeof(minic_riscv64_inline_asm_registers) / sizeof(minic_riscv64_inline_asm_registers[0]))\n''',
    "register candidates",
)

insert_after = '''static bool inline_asm_clobbers_register(const MinicInlineAsm *inline_asm,\n                                         const char *register_name) {\n    size_t index;\n    size_t register_length;\n\n    if (inline_asm == NULL || register_name == NULL) {\n        return false;\n    }\n    register_length = strlen(register_name);\n    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {\n        const MinicInlineAsmRegisterClobber *clobber;\n\n        clobber = &inline_asm->register_clobbers[index];\n        if (clobber->name != NULL && clobber->name_length == register_length &&\n            memcmp(clobber->name, register_name, register_length) == 0) {\n            return true;\n        }\n    }\n    return false;\n}\n'''
addition = insert_after + '''\nstatic bool inline_asm_register_is_callee_saved(const char *register_name) {\n    size_t index;\n\n    if (register_name == NULL) {\n        return false;\n    }\n    for (index = 0U; index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT; ++index) {\n        if (strcmp(minic_riscv64_inline_asm_registers[index].name, register_name) == 0) {\n            return minic_riscv64_inline_asm_registers[index].is_callee_saved;\n        }\n    }\n    return false;\n}\n\nstatic bool append_saved_operand_register(const char *register_name,\n                                          const char **saved_registers,\n                                          size_t *saved_register_count) {\n    size_t index;\n\n    if (register_name == NULL || saved_registers == NULL || saved_register_count == NULL ||\n        *saved_register_count >= MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS) {\n        return false;\n    }\n    if (!inline_asm_register_is_callee_saved(register_name)) {\n        return true;\n    }\n    for (index = 0U; index < *saved_register_count; ++index) {\n        if (strcmp(saved_registers[index], register_name) == 0) {\n            return true;\n        }\n    }\n    saved_registers[*saved_register_count] = register_name;\n    *saved_register_count += 1U;\n    return true;\n}\n'''
text = replace_once(text, insert_after, addition, "callee-saved helpers")

old_assign = '''        while (candidate_index < MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS &&\n               inline_asm_clobbers_register(inline_asm,\n                                            minic_riscv64_inline_asm_registers[candidate_index])) {\n            candidate_index += 1U;\n        }\n        if (candidate_index >= MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS) {\n            return false;\n        }\n        operand_registers[operand_index] = minic_riscv64_inline_asm_registers[candidate_index];\n        candidate_index += 1U;\n'''
new_assign = '''        while (candidate_index < MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT &&\n               (inline_asm_clobbers_register(\n                    inline_asm, minic_riscv64_inline_asm_registers[candidate_index].name) ||\n                (inline_asm->is_goto &&\n                 minic_riscv64_inline_asm_registers[candidate_index].is_callee_saved))) {\n            candidate_index += 1U;\n        }\n        if (candidate_index >= MINIC_RISCV64_INLINE_ASM_REGISTER_COUNT) {\n            return false;\n        }\n        operand_registers[operand_index] = minic_riscv64_inline_asm_registers[candidate_index].name;\n        candidate_index += 1U;\n'''
text = replace_once(text, old_assign, new_assign, "register assignment")

old_vars = '''    const char *operand_registers[MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS];\n    size_t operand_count;\n    size_t temporary_size;\n    size_t index;\n'''
new_vars = '''    const char *operand_registers[MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS];\n    const char *saved_registers[MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS];\n    size_t operand_count;\n    size_t saved_register_count;\n    size_t temporary_slot_count;\n    size_t temporary_size;\n    size_t index;\n'''
text = replace_once(text, old_vars, new_vars, "inline asm locals")

old_after_assign = '''    if (!assign_operand_registers(inline_asm, program, operand_registers, operand_count)) {\n        return false;\n    }\n\n    for (index = 0U; index < inline_asm->input_count; ++index) {\n'''
new_after_assign = '''    if (!assign_operand_registers(inline_asm, program, operand_registers, operand_count)) {\n        return false;\n    }\n    saved_register_count = 0U;\n    for (index = 0U; index < operand_count; ++index) {\n        if (operand_registers[index] != NULL &&\n            !append_saved_operand_register(\n                operand_registers[index], saved_registers, &saved_register_count)) {\n            return false;\n        }\n    }\n    if (inline_asm->is_goto && saved_register_count != 0U) {\n        return false;\n    }\n\n    for (index = 0U; index < inline_asm->input_count; ++index) {\n'''
text = replace_once(text, old_after_assign, new_after_assign, "saved register collection")

old_temp = '''    if (operand_count > (SIZE_MAX - 15U) / 8U) {\n        return false;\n    }\n    temporary_size = inline_asm->is_goto ? 0U : (operand_count * 8U + 15U) & ~(size_t)15U;\n    if (!minic_riscv64_emit_stack_allocate(file, temporary_size)) {\n        return false;\n    }\n\n    for (index = 0U; index < inline_asm->output_count; ++index) {\n'''
new_temp = '''    if (operand_count > SIZE_MAX - saved_register_count) {\n        return false;\n    }\n    temporary_slot_count = operand_count + saved_register_count;\n    if (temporary_slot_count > (SIZE_MAX - 15U) / 8U) {\n        return false;\n    }\n    temporary_size =\n        inline_asm->is_goto ? 0U : (temporary_slot_count * 8U + 15U) & ~(size_t)15U;\n    if (!minic_riscv64_emit_stack_allocate(file, temporary_size)) {\n        return false;\n    }\n    for (index = 0U; index < saved_register_count; ++index) {\n        if (!minic_riscv64_emit_sp_store64(\n                file, saved_registers[index], (operand_count + index) * 8U)) {\n            return false;\n        }\n    }\n\n    for (index = 0U; index < inline_asm->output_count; ++index) {\n'''
text = replace_once(text, old_temp, new_temp, "temporary frame")

old_return = '''    }\n    return minic_riscv64_emit_stack_release(file, temporary_size);\n}\n'''
new_return = '''    }\n    for (index = 0U; index < saved_register_count; ++index) {\n        if (!minic_riscv64_emit_sp_load64(\n                file, saved_registers[index], (operand_count + index) * 8U)) {\n            return false;\n        }\n    }\n    return minic_riscv64_emit_stack_release(file, temporary_size);\n}\n'''
# Use the final occurrence belonging to minic_riscv64_emit_inline_asm.
pos = text.rfind(old_return)
if pos < 0:
    raise SystemExit("inline asm return: anchor missing")
text = text[:pos] + new_return + text[pos + len(old_return):]
path.write_text(text)

runner = Path("tests/compiler/c0/run-gnu-inline-asm-operands.sh")
text = runner.read_text()
anchor = '''grep -F 'add t0, t0, zero' "$work/argument-clobber.s" >/dev/null\n\ncat >"$work/unsupported-clobber.c" <<'EOF'\n'''
insert = '''grep -F 'add t0, t0, zero' "$work/argument-clobber.s" >/dev/null\n\ncat >"$work/callee-saved-fallback.c" <<'EOF'\nlong f(long left, long right) {\n    long result;\n\n    __asm__ __volatile__("add %0, %1, %2"\n                         : "=r"(result)\n                         : "r"(left), "r"(right)\n                         : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",\n                           "t0", "t1", "t2", "t3", "t4", "t5", "t6");\n    return result;\n}\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/callee-saved-fallback.c" \\\n    -o "$work/callee-saved-fallback.i"\n"$minic" -S "$work/callee-saved-fallback.i" -o "$work/callee-saved-fallback.s"\ngrep -F '  sd s1, 24(sp)' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F '  sd s2, 32(sp)' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F '  sd s3, 40(sp)' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F 'add s1, s2, s3' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F '  ld s1, 24(sp)' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F '  ld s2, 32(sp)' "$work/callee-saved-fallback.s" >/dev/null\ngrep -F '  ld s3, 40(sp)' "$work/callee-saved-fallback.s" >/dev/null\n\ncat >"$work/unsupported-clobber.c" <<'EOF'\n'''
text = replace_once(text, anchor, insert, "inline asm focused regression")
text = text.replace(
    "clobber=memory,t3 reservation=t3->t4 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'",
    "clobber=memory,t3 reservation=t3->t4 callee-saved-fallback=s1-s3 immediates=rv64-I placeholders=0,1,2 staging=stack target=RV64'",
    1,
)
runner.write_text(text)
