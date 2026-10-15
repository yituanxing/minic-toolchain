from pathlib import Path

lower_path = Path("src/core/core_lower.c")
lower = lower_path.read_text()
old_lower = '''    /* BATCH_G_TWO_SCALAR_OUTPUTLESS_ASM: outputless GNU asm is effectively
       volatile (Batch F). Reuse the generic structured operand model when the
       statement has two scalar register inputs and a memory clobber. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
'''
new_lower = '''    /* BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY: outputless GNU asm is
       effectively volatile (Batch F).  The two-register-input structured form
       is valid both for ordering-sensitive asm carrying a memory clobber and
       for MMIO-style asm whose template itself performs the access.  Preserve
       the actual memory effect flag rather than requiring one to exist. */
    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 2U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
if old_lower not in lower:
    if "BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY" not in lower:
        raise SystemExit("Batch X lowerer shape anchor not found")
else:
    lower = lower.replace(old_lower, new_lower, 1)
old_add = '''                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              true,
                                                              &inline_asm_id);
'''
new_add = '''                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              numeric_template,
                                                              numeric_template_length,
                                                              true,
                                                              source->has_memory_clobber,
                                                              &inline_asm_id);
'''
marker = "BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY"
marker_pos = lower.find(marker)
if marker_pos < 0:
    raise SystemExit("Batch X lowerer marker missing")
add_pos = lower.find(old_add, marker_pos)
if add_pos >= 0:
    lower = lower[:add_pos] + new_add + lower[add_pos + len(old_add):]
elif lower.find(new_add, marker_pos) < 0:
    raise SystemExit("Batch X opaque-asm flag anchor not found")
lower_path.write_text(lower)

backend_path = Path("src/target/riscv64/core_codegen.c")
backend = backend_path.read_text()
old_backend = '''          (register_outputs == 0U && register_readwrites == 0U &&
           memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U &&
           inline_asm->has_memory_clobber) ||
'''
new_backend = '''          /* BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY: the emitted
             instruction operands are identical with or without a compiler
             memory clobber. Keep register clobbers excluded for this shape,
             but preserve either memory-effect setting. */
          (register_outputs == 0U && register_readwrites == 0U &&
           memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U &&
           inline_asm->register_clobber_count == 0U) ||
'''
if old_backend not in backend:
    if "BATCH_X_TWO_SCALAR_OUTPUTLESS_ASM_OPTIONAL_MEMORY" not in backend:
        raise SystemExit("Batch X backend shape anchor not found")
else:
    backend = backend.replace(old_backend, new_backend, 1)
backend_path.write_text(backend)
print("CORE_BATCH_X_PATCHED two-input outputless asm optional memory clobber")
