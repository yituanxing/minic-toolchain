#ifndef MINIC_TARGET_RISCV_TYPED_MEMORY_H
#define MINIC_TARGET_RISCV_TYPED_MEMORY_H

#include "core/address_alignment.h"
#include "target/data_layout.h"
#include "target/riscv/register_identity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct MinicRiscvAddressLayout {
    const MinicC0Program *program;
    const MinicDataLayout *data_layout;
} MinicRiscvAddressLayout;

/* Root resolution never assumes a pointer's nominal pointee alignment.
 * Local CoreObjects have target DataLayout and explicit frame alignment;
 * global symbols are conservatively unknown until CoreGlobal also owns
 * their declared packed/explicit alignment and external-definition policy. */
static inline size_t minic_riscv_address_root_alignment(
    const MinicCoreFunction *function,
    const MinicCoreInstruction *root, void *opaque) {
    const MinicRiscvAddressLayout *layout = opaque;
    const MinicCoreObject *object;
    size_t bytes, alignment;
    if (function == NULL || root == NULL || layout == NULL ||
        layout->program == NULL || layout->data_layout == NULL ||
        root->kind != MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
        root->value.object_id >= function->object_count ||
        function->objects == NULL)
        return 1U;
    object = &function->objects[root->value.object_id];
    if (!minic_data_layout_type(layout->data_layout, layout->program,
                                object->type, &bytes, &alignment))
        return 1U;
    (void)bytes;
    if (object->explicit_alignment > alignment)
        alignment = object->explicit_alignment;
    return alignment;
}

static inline size_t minic_riscv_address_alignment(
    const MinicC0Program *program, const MinicDataLayout *data_layout,
    const MinicCoreFunction *function, MinicCoreValueId address) {
    MinicRiscvAddressLayout context = {program, data_layout};
    return minic_core_address_known_alignment(
        function, address, minic_riscv_address_root_alignment, &context);
}

/* One target-local RISC-V Base-I scalar memory transport for both XLENs.
 * A positive SSA proof admits an unconditional native machine operation.
 * Otherwise guard the machine-width fast path and transport exact bytes on
 * genuinely unaligned addresses. No instruction from an optional ISA
 * extension is ever synthesized.  scratch must not alias either live input.
 * Sign extension matches lh/lw on RV64 and lh on RV32. */
static inline bool minic_riscv_emit_typed_memory(
    FILE *out, bool store, unsigned xlen_bits, size_t width,
    bool is_signed, const char *native_opcode,
    const char *value_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment,
    const char *label_domain) {
    /* This counter is monotonic per emitter translation unit. Each call
     * site supplies a distinct target-file domain, so concurrent RV32/RV64
     * emitters cannot reuse a textual assembler label in one output file.
     * Never consume numeric local labels: outer overflow/asm CFG uses 1f/2f. */
    static size_t next_label = 0U;
    size_t label;
    size_t byte;
    if (out == NULL || native_opcode == NULL || *native_opcode == '\0' ||
        value_register == NULL || address_register == NULL ||
        scratch_register == NULL || label_domain == NULL ||
        *label_domain == '\0' ||
        (xlen_bits != 32U && xlen_bits != 64U) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        width > xlen_bits / 8U ||
        minic_riscv_gpr_name_equal(value_register, address_register) ||
        minic_riscv_gpr_name_equal(scratch_register, value_register) ||
        minic_riscv_gpr_name_equal(scratch_register, address_register))
        return false;
    if (width == 1U || proven_alignment >= width)
        return fprintf(out, "  %s %s, 0(%s)\n", native_opcode,
                       value_register, address_register) >= 0;
    if (next_label == SIZE_MAX) return false;
    label = ++next_label;
    if (fprintf(out,
                "  andi %s, %s, %zu\n"
                "  beqz %s, .Lminic_typed_%s_%zu_native\n",
                scratch_register, address_register, width - 1U,
                scratch_register, label_domain, label) < 0)
        return false;
    if (store) {
        if (fprintf(out, "  mv %s, %s\n", scratch_register,
                    value_register) < 0) return false;
        for (byte = 0U; byte < width; ++byte) {
            if (fprintf(out, "  sb %s, %zu(%s)\n", scratch_register,
                        byte, address_register) < 0 ||
                (byte + 1U < width &&
                 fprintf(out, "  srli %s, %s, 8\n",
                         scratch_register, scratch_register) < 0))
                return false;
        }
    } else {
        if (fprintf(out, "  li %s, 0\n", value_register) < 0)
            return false;
        for (byte = 0U; byte < width; ++byte) {
            if (fprintf(out, "  lbu %s, %zu(%s)\n",
                        scratch_register, byte, address_register) < 0 ||
                (byte != 0U &&
                 fprintf(out, "  slli %s, %s, %zu\n",
                         scratch_register, scratch_register, byte * 8U) < 0) ||
                fprintf(out, "  or %s, %s, %s\n", value_register,
                        value_register, scratch_register) < 0)
                return false;
        }
        if (is_signed && width * 8U < xlen_bits &&
            fprintf(out, "  slli %s, %s, %u\n  srai %s, %s, %u\n",
                    value_register, value_register,
                    xlen_bits - (unsigned)(width * 8U),
                    value_register, value_register,
                    xlen_bits - (unsigned)(width * 8U)) < 0)
            return false;
    }
    return fprintf(out,
                   "  j .Lminic_typed_%s_%zu_done\n"
                   ".Lminic_typed_%s_%zu_native:\n"
                   "  %s %s, 0(%s)\n"
                   ".Lminic_typed_%s_%zu_done:\n",
                   label_domain, label, label_domain, label,
                   native_opcode, value_register, address_register,
                   label_domain, label) >= 0;
}

/* A 2*XLEN C scalar remains one semantic Core value but occupies two
 * little-endian machine words. The address is dead after the transfer;
 * advance it by one XLEN between independently guarded accesses. */
static inline bool minic_riscv_emit_typed_word_pair(
    FILE *out, bool store, unsigned xlen_bits,
    const char *low_register, const char *high_register,
    const char *address_register, const char *scratch_register,
    size_t proven_alignment, const char *label_domain) {
    const size_t word = xlen_bits / 8U;
    const char *opcode = xlen_bits == 32U
        ? (store ? "sw" : "lw") : (store ? "sd" : "ld");
    return (xlen_bits == 32U || xlen_bits == 64U) &&
           !minic_riscv_gpr_name_equal(low_register, high_register) &&
           !minic_riscv_gpr_name_equal(high_register, address_register) &&
           !minic_riscv_gpr_name_equal(high_register, scratch_register) &&
           minic_riscv_emit_typed_memory(
               out, store, xlen_bits, word, false, opcode,
               low_register, address_register, scratch_register,
               proven_alignment, label_domain) &&
           fprintf(out, "  addi %s, %s, %zu\n",
                   address_register, address_register, word) >= 0 &&
           minic_riscv_emit_typed_memory(
               out, store, xlen_bits, word, false, opcode,
               high_register, address_register, scratch_register,
               proven_alignment, label_domain);
}

#endif
