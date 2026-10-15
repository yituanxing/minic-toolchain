from pathlib import Path

MARKER = 'M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS'


def replace_once(text: str, anchor: str, replacement: str, label: str) -> str:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'M68 {label} anchor count={count}')
    return text.replace(anchor, replacement, 1)


# The M67 Core instruction already models a variable operand list. Widen only
# the lowering admission rule: the same two-register-output + one +A memory
# family legitimately appears with zero, one, or two scalar register inputs.
path = Path('src/core/core_lower.c')
text = path.read_text()
if MARKER not in text:
    text = replace_once(
        text,
        '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL && source->inputs != NULL &&
        source->output_count == 3U && source->input_count == 2U && source->has_memory_clobber &&
''',
        '''    /* M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: M67's structured
       operand model is variable-sized. Admit the same proven output/memory
       shape with 0..2 scalar register inputs instead of hard-coding two. */
    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->outputs != NULL &&
        (source->input_count == 0U || source->inputs != NULL) &&
        source->output_count == 3U && source->input_count <= 2U && source->has_memory_clobber &&
''',
        'lowering-shape')
    path.write_text(text)
else:
    print('M68 core_lower.c already applied')


# RV64 has five scratch registers reserved by this tier, but it need not use
# all of them. Keep the semantic requirement of exactly two register outputs
# and one read/write memory operand, and accept 0..2 scalar inputs.
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
if MARKER not in text:
    text = replace_once(
        text,
        '''/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic;
   this RV64 emission tier currently accepts the proven 2 register outputs +
   1 read/write memory + 2 scalar inputs shape. */
''',
        '''/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic.
   M68_STRUCTURED_INLINE_ASM_OPTIONAL_INPUTS: this RV64 tier accepts the
   proven 2 register outputs + 1 read/write memory + 0..2 scalar inputs family. */
''',
        'backend-comment')
    text = replace_once(
        text,
        '''        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count != 5U) {
''',
        '''        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count < 3U ||
        instruction->value.structured_inline_asm.operand_count > 5U) {
''',
        'backend-count-guard')
    text = replace_once(
        text,
        '''    if (register_outputs != 2U || memory_readwrites != 1U || scalar_inputs != 2U) {
        return false;
    }
''',
        '''    if (register_outputs != 2U || memory_readwrites != 1U || scalar_inputs > 2U ||
        scalar_inputs + 3U != instruction->value.structured_inline_asm.operand_count) {
        return false;
    }
''',
        'backend-role-counts')
    path.write_text(text)
else:
    print('M68 core_codegen.c already applied')

print('M68 structured inline asm optional scalar inputs applied')
