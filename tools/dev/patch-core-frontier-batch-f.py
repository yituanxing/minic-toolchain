#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}")
    p.write_text(text.replace(old, new, 1))


path = "src/core/core_lower.c"

# Batch F: GNU outputless asm is implicitly volatile even without an explicit
# `volatile` token. The frontend preserves source spelling in is_volatile, so
# Core must use effective volatility for outputless effect statements. This is
# a semantic property, not a Linux special case.
old = '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
'''
new = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U &&
        source->input_count != 0U && source->inputs != NULL && source->label_count == 0U &&
        source->register_clobber_count == 0U && !source->has_memory_clobber &&
        source->clobber_count == 0U) {
'''
replace_once(path, old, new)

old = '''                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              specialized_template,
                                                              specialized_length,
                                                              source->is_volatile,
                                                              false,
                                                              &inline_asm_id);
'''
new = '''                    minic_core_function_add_opaque_inline_asm(context->function,
                                                              specialized_template,
                                                              specialized_length,
                                                              true,
                                                              false,
                                                              &inline_asm_id);
'''
replace_once(path, old, new)

old = '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
'''
new = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length == 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U && source->has_memory_clobber &&
        source->clobber_count == 1U) {
'''
replace_once(path, old, new)

old = '''    if (source->is_volatile && !source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
new = '''    if (!source->is_goto && source->template_text != NULL &&
        source->template_length != 0U && source->output_count == 0U && source->inputs != NULL &&
        source->input_count == 1U && source->label_count == 0U &&
        source->register_clobber_count == 0U &&
        source->clobber_count == (source->has_memory_clobber ? 1U : 0U)) {
'''
replace_once(path, old, new)

# In the outputless path, Core stores semantic volatility rather than source
# spelling so verifier/backend see the real GNU semantics.
old = '''            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           source->is_volatile,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
'''
new = '''            if (!minic_core_function_add_opaque_inline_asm(context->function,
                                                           source->template_text,
                                                           source->template_length,
                                                           true,
                                                           source->has_memory_clobber,
                                                           &inline_asm_id)) {
'''
# This text occurs in multiple paths. Patch only the final outputless scalar-input
# path by replacing the last occurrence.
p = Path(path)
text = p.read_text()
pos = text.rfind(old)
if pos < 0:
    if new not in text:
        raise SystemExit("outputless scalar-input opaque-asm anchor not found")
else:
    p.write_text(text[:pos] + new + text[pos + len(old):])

print("CORE_BATCH_F_PATCHED outputless-asm effective-volatility")
