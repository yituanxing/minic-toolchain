#ifndef MINIC_TARGET_RISCV_FRAME_ADDRESS_POLICY_H
#define MINIC_TARGET_RISCV_FRAME_ADDRESS_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* A machine frame access must never overwrite its data/base register while
 * materializing a displacement larger than the signed 12-bit load/store
 * immediate. The caller owns the scratch liveness contract and supplies an
 * alternate register for the case where the preferred scratch is the value.
 * This policy is XLEN-neutral: the target supplies its own word opcodes. */
typedef struct MinicRiscvFrameAddressPolicy {
    const char *base_register;
    const char *preferred_scratch;
    const char *alternate_scratch;
    const char *load_opcode;
    const char *store_opcode;
} MinicRiscvFrameAddressPolicy;

static inline bool minic_riscv_frame_register_available(
    const char *candidate, const char *base, const char *value) {
    return candidate != NULL && candidate[0] != '\0' &&
           strcmp(candidate, base) != 0 &&
           (value == NULL || strcmp(candidate, value) != 0);
}

static inline const char *minic_riscv_frame_select_scratch(
    const MinicRiscvFrameAddressPolicy *policy, const char *value,
    const char *protected_register) {
    if (policy == NULL || policy->base_register == NULL) return NULL;
    if (minic_riscv_frame_register_available(
            policy->preferred_scratch, policy->base_register, value) &&
        (protected_register == NULL ||
         strcmp(policy->preferred_scratch, protected_register) != 0))
        return policy->preferred_scratch;
    if (minic_riscv_frame_register_available(
            policy->alternate_scratch, policy->base_register, value) &&
        (protected_register == NULL ||
         strcmp(policy->alternate_scratch, protected_register) != 0))
        return policy->alternate_scratch;
    return NULL;
}

static inline bool minic_riscv_frame_emit_address(
    FILE *out, const MinicRiscvFrameAddressPolicy *policy,
    const char *destination, size_t offset) {
    if (out == NULL || policy == NULL || policy->base_register == NULL ||
        destination == NULL || destination[0] == '\0' ||
        strcmp(destination, policy->base_register) == 0)
        return false;
    if (offset <= 2047U)
        return fprintf(out, "  addi %s, %s, %zu\n",
                       destination, policy->base_register, offset) >= 0;
    /* Address materialization owns its destination: no scratch register needs
     * to be clobbered, even if the caller keeps t2/t5/t6 live. */
    return fprintf(out, "  li %s, %zu\n  add %s, %s, %s\n",
                   destination, offset, destination,
                   policy->base_register, destination) >= 0;
}

static inline bool minic_riscv_frame_emit_word_preserving(
    FILE *out, const MinicRiscvFrameAddressPolicy *policy,
    bool store, const char *value_register,
    const char *protected_register, size_t offset) {
    const char *scratch;
    const char *opcode;
    if (out == NULL || policy == NULL || policy->base_register == NULL ||
        value_register == NULL || value_register[0] == '\0')
        return false;
    opcode = store ? policy->store_opcode : policy->load_opcode;
    if (opcode == NULL || opcode[0] == '\0') return false;
    if (offset <= 2047U)
        return fprintf(out, "  %s %s, %zu(%s)\n",
                       opcode, value_register, offset,
                       policy->base_register) >= 0;
    scratch = minic_riscv_frame_select_scratch(
        policy, value_register, protected_register);
    return scratch != NULL &&
           fprintf(out, "  li %s, %zu\n  add %s, %s, %s\n"
                        "  %s %s, 0(%s)\n",
                   scratch, offset, scratch, policy->base_register, scratch,
                   opcode, value_register, scratch) >= 0;
}

static inline bool minic_riscv_frame_emit_word(
    FILE *out, const MinicRiscvFrameAddressPolicy *policy,
    bool store, const char *value_register, size_t offset) {
    return minic_riscv_frame_emit_word_preserving(
        out, policy, store, value_register, NULL, offset);
}

#endif
