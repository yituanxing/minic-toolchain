#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_ASM_FORM_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_ASM_FORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stable form admission independent of the old FILE* direct emitter.  The
 * source boundary and the typed MachineIR boundary consult the same exact
 * target decoder, never a permissive textual fallback. */
bool minic_loongarch64_typed_descriptor_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length);
bool minic_loongarch64_descriptor_template_requires_typed_id(
    const char *text, size_t length);
bool minic_loongarch64_typed_scalar_form_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length);
bool minic_loongarch64_scalar_template_requires_typed_id(
    const char *text, size_t length);

/* Linux __jump_table's key operand can address an array/object member.
 * Core has already proved the symbol and signed byte displacement; preserve
 * that exact relocation range instead of assuming only key/key+1. */
bool minic_loongarch64_jump_key_relocation_valid(
    const char *symbol, int64_t addend);

#endif
