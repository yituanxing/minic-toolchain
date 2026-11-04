/* LoongArch64 ELF64 ET_EXEC psABI classification and machine fields.
 * PCALA's rounded high part and GOT/TLS slots belong to the target leaf. */

#include "linker_loongarch_static_got.inc"

static MiniLdNonRiscvFixup minild_loongarch_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_LARCH_NONE:
    case R_LARCH_MARK_LA:
    case R_LARCH_MARK_PCREL:
    case R_LARCH_RELAX:
    case R_LARCH_ALIGN: return MINILD_NONRISCV_NONE;
    case R_LARCH_64: return MINILD_NONRISCV_ABS64;
    case R_LARCH_32: return MINILD_NONRISCV_ABS32;
    case R_LARCH_64_PCREL: return MINILD_NONRISCV_PCREL64;
    case R_LARCH_32_PCREL: return MINILD_NONRISCV_PCREL32;
    case R_LARCH_B16:
    case R_LARCH_B21:
    case R_LARCH_B26:
    case R_LARCH_PCREL20_S2:
    case R_LARCH_CALL36:
    case R_LARCH_ABS_HI20:
    case R_LARCH_ABS_LO12:
    case R_LARCH_ABS64_LO20:
    case R_LARCH_ABS64_HI12:
    case R_LARCH_PCALA_HI20:
    case R_LARCH_PCALA_LO12:
    case R_LARCH_PCALA64_LO20:
    case R_LARCH_PCALA64_HI12:
    case R_LARCH_ADD6:
    case R_LARCH_SUB6:
    case R_LARCH_ADD8:
    case R_LARCH_SUB8:
    case R_LARCH_ADD16:
    case R_LARCH_SUB16:
    case R_LARCH_ADD24:
    case R_LARCH_SUB24:
    case R_LARCH_ADD32:
    case R_LARCH_SUB32:
    case R_LARCH_ADD64:
    case R_LARCH_SUB64:
    case R_LARCH_TLS_TPREL32:
    case R_LARCH_TLS_TPREL64:
    case R_LARCH_TLS_LE_HI20:
    case R_LARCH_TLS_LE_LO12:
    case R_LARCH_TLS_LE64_LO20:
    case R_LARCH_TLS_LE64_HI12:
    case R_LARCH_TLS_LE_HI20_R:
    case R_LARCH_TLS_LE_LO12_R:
    case R_LARCH_TLS_LE_ADD_R:
    case R_LARCH_GOT_PC_HI20:
    case R_LARCH_GOT_PC_LO12:
    case R_LARCH_GOT64_PC_LO20:
    case R_LARCH_GOT64_PC_HI12:
    case R_LARCH_GOT_HI20:
    case R_LARCH_GOT_LO12:
    case R_LARCH_GOT64_LO20:
    case R_LARCH_GOT64_HI12:
    case R_LARCH_TLS_IE_PC_HI20:
    case R_LARCH_TLS_IE_PC_LO12:
    case R_LARCH_TLS_IE64_PC_LO20:
    case R_LARCH_TLS_IE64_PC_HI12:
    case R_LARCH_TLS_IE_HI20:
    case R_LARCH_TLS_IE_LO12:
    case R_LARCH_TLS_IE64_LO20:
    case R_LARCH_TLS_IE64_HI12:
        return MINILD_NONRISCV_TARGET_INSTRUCTION;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static size_t minild_loongarch_static_instruction_width(uint32_t type) {
    switch (type) {
    case R_LARCH_ADD6:
    case R_LARCH_SUB6:
    case R_LARCH_ADD8:
    case R_LARCH_SUB8: return 1U;
    case R_LARCH_ADD16:
    case R_LARCH_SUB16: return 2U;
    case R_LARCH_ADD24:
    case R_LARCH_SUB24: return 3U;
    case R_LARCH_ADD64:
    case R_LARCH_SUB64:
    case R_LARCH_TLS_TPREL64:
    case R_LARCH_CALL36: return 8U;
    default: return 4U;
    }
}

/* The architectural pcalau12i adjustment compensates the low immediate's
 * sign extension and the follow-up lu32i.d / lu52i.d instructions.  This is
 * the LoongArch psABI's signed rounded page delta, not (S-P+0x800)>>12. */
static uint64_t minild_loongarch_page_delta(uint64_t target, uint64_t place,
                                            uint32_t type) {
    uint64_t pc = place;
    uint64_t value;

    if (type == R_LARCH_PCALA64_LO20 ||
        type == R_LARCH_GOT64_PC_LO20 ||
        type == R_LARCH_TLS_IE64_PC_LO20) {
        pc -= 8U;
    } else if (type == R_LARCH_PCALA64_HI12 ||
               type == R_LARCH_GOT64_PC_HI12 ||
               type == R_LARCH_TLS_IE64_PC_HI12) {
        pc -= 12U;
    }
    value = (target & ~UINT64_C(0xfff)) - (pc & ~UINT64_C(0xfff));
    if ((target & UINT64_C(0x800)) != 0U) {
        value += UINT64_C(0x1000) - UINT64_C(0x100000000);
    }
    if ((value & UINT64_C(0x80000000)) != 0U) {
        value += UINT64_C(0x100000000);
    }
    return value;
}

static bool minild_loongarch_low12_opcode(uint32_t word) {
    uint32_t op = word & UINT32_C(0xffc00000);

    /* Signed/unsigned arithmetic immediates and byte/word/dword memory
     * accesses are the only K12 encodings in the non-vector base ISA. */
    switch (op) {
    case UINT32_C(0x02000000):
    case UINT32_C(0x02400000):
    case UINT32_C(0x02800000):
    case UINT32_C(0x02c00000):
    case UINT32_C(0x03400000):
    case UINT32_C(0x03800000):
    case UINT32_C(0x03c00000):
    case UINT32_C(0x28000000):
    case UINT32_C(0x28400000):
    case UINT32_C(0x28800000):
    case UINT32_C(0x28c00000):
    case UINT32_C(0x29000000):
    case UINT32_C(0x29400000):
    case UINT32_C(0x29800000):
    case UINT32_C(0x29c00000):
    case UINT32_C(0x2a000000):
    case UINT32_C(0x2a400000):
    case UINT32_C(0x2a800000): return true;
    default: return false;
    }
}

static bool minild_loongarch_branch_opcode(uint32_t type, uint32_t word) {
    uint32_t op = word & UINT32_C(0xfc000000);

    switch (type) {
    case R_LARCH_B16:
        return op >= UINT32_C(0x58000000) &&
               op <= UINT32_C(0x6c000000);
    case R_LARCH_B21:
        return op == UINT32_C(0x40000000) ||
               op == UINT32_C(0x44000000) ||
               op == UINT32_C(0x48000000);
    case R_LARCH_B26:
        return op == UINT32_C(0x50000000) ||
               op == UINT32_C(0x54000000);
    default: return false;
    }
}

static bool minild_loongarch_static_write_instruction(
    unsigned char *slot, const MiniLdReloc *reloc,
    uint64_t target, uint64_t place, bool undefined_weak) {
    uint64_t delta = target - place;
    uint64_t value = target;
    uint32_t instruction = 0U;
    uint32_t imm;
    size_t width;
    size_t i;
    bool sub;

    (void)undefined_weak;
    if (minild_loongarch_static_instruction_width(reloc->type) >= 4U) {
        instruction = load_u32le(slot);
    }

    switch (reloc->type) {
    case R_LARCH_B16:
    case R_LARCH_B21:
    case R_LARCH_B26:
    case R_LARCH_PCREL20_S2: {
        unsigned bits = reloc->type == R_LARCH_B16 ? 18U :
                        reloc->type == R_LARCH_B21 ? 23U :
                        reloc->type == R_LARCH_PCREL20_S2 ? 22U : 28U;

        if ((reloc->type == R_LARCH_PCREL20_S2 &&
             (instruction & UINT32_C(0xfe000000)) !=
                 UINT32_C(0x18000000)) ||
            (reloc->type != R_LARCH_PCREL20_S2 &&
             !minild_loongarch_branch_opcode(reloc->type, instruction)) ||
            (delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(delta, bits)) {
            return false;
        }
        if (reloc->type == R_LARCH_B16) {
            instruction = (instruction & ~UINT32_C(0x03fffc00)) |
                          ((((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U);
        } else if (reloc->type == R_LARCH_B21) {
            instruction = (instruction & ~UINT32_C(0x03fffc1f)) |
                          ((((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U) |
                          (((uint32_t)delta >> 18U) & UINT32_C(0x1f));
        } else if (reloc->type == R_LARCH_B26) {
            instruction = (instruction & ~UINT32_C(0x03ffffff)) |
                          ((((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U) |
                          (((uint32_t)delta >> 18U) & UINT32_C(0x3ff));
        } else {
            instruction = (instruction & ~UINT32_C(0x01ffffe0)) |
                          ((((uint32_t)delta >> 2U) & UINT32_C(0xfffff)) << 5U);
        }
        store_u32le(slot, instruction);
        return true;
    }
    case R_LARCH_CALL36: {
        uint64_t rounded = delta + UINT64_C(0x20000);
        uint32_t next = load_u32le(slot + 4U);

        if ((instruction & UINT32_C(0xfe000000)) != UINT32_C(0x1e000000) ||
            (next & UINT32_C(0xfc000000)) != UINT32_C(0x4c000000) ||
            (delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(rounded, 38U)) {
            return false;
        }
        instruction = (instruction & ~UINT32_C(0x01ffffe0)) |
                      ((((uint32_t)(rounded >> 18U)) & UINT32_C(0xfffff)) << 5U);
        next = (next & ~UINT32_C(0x03fffc00)) |
               ((((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U);
        store_u32le(slot, instruction);
        store_u32le(slot + 4U, next);
        return true;
    }
    case R_LARCH_ABS_HI20:
    case R_LARCH_PCALA_HI20:
    case R_LARCH_GOT_PC_HI20:
    case R_LARCH_GOT_HI20:
    case R_LARCH_TLS_IE_PC_HI20:
    case R_LARCH_TLS_IE_HI20:
    case R_LARCH_TLS_LE_HI20:
    case R_LARCH_TLS_LE_HI20_R:
        if ((instruction & UINT32_C(0xfe000000)) !=
                ((reloc->type == R_LARCH_PCALA_HI20 ||
                  reloc->type == R_LARCH_GOT_PC_HI20 ||
                  reloc->type == R_LARCH_TLS_IE_PC_HI20)
                     ? UINT32_C(0x1a000000) : UINT32_C(0x14000000))) {
            return false;
        }
        if (reloc->type == R_LARCH_PCALA_HI20 ||
            reloc->type == R_LARCH_GOT_PC_HI20 ||
            reloc->type == R_LARCH_TLS_IE_PC_HI20) {
            value = minild_loongarch_page_delta(target, place, reloc->type);
            if (!minild_nonriscv_signed_fits(value, 32U)) {
                return false;
            }
        } else if (reloc->type == R_LARCH_TLS_LE_HI20_R) {
            value += UINT64_C(0x800);
        }
        instruction = (instruction & ~UINT32_C(0x01ffffe0)) |
                      ((((uint32_t)(value >> 12U)) & UINT32_C(0xfffff)) << 5U);
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_ABS_LO12:
    case R_LARCH_PCALA_LO12:
    case R_LARCH_GOT_PC_LO12:
    case R_LARCH_GOT_LO12:
    case R_LARCH_TLS_IE_PC_LO12:
    case R_LARCH_TLS_IE_LO12:
    case R_LARCH_TLS_LE_LO12:
    case R_LARCH_TLS_LE_LO12_R:
        if (!minild_loongarch_low12_opcode(instruction) &&
            !(reloc->type == R_LARCH_PCALA_LO12 &&
              (instruction & UINT32_C(0xfc000000)) ==
                  UINT32_C(0x4c000000))) {
            return false;
        }
        if ((reloc->type == R_LARCH_GOT_PC_LO12 ||
             reloc->type == R_LARCH_GOT_LO12 ||
             reloc->type == R_LARCH_TLS_IE_PC_LO12 ||
             reloc->type == R_LARCH_TLS_IE_LO12) &&
            (instruction & UINT32_C(0xffc00000)) !=
                UINT32_C(0x28c00000)) {
            return false;
        }
        if (reloc->type == R_LARCH_PCALA_LO12 &&
            (instruction & UINT32_C(0xfc000000)) == UINT32_C(0x4c000000)) {
            if ((target & UINT64_C(3)) != 0U) {
                return false;
            }
            imm = (((uint32_t)target & UINT32_C(0xfff)) >> 2U) |
                  (((uint32_t)target & UINT32_C(0x800)) != 0U
                      ? UINT32_C(0xfc00) : 0U);
            instruction = (instruction & ~UINT32_C(0x03fffc00)) |
                          (imm << 10U);
        } else {
            instruction = (instruction & ~UINT32_C(0x003ffc00)) |
                          (((uint32_t)target & UINT32_C(0xfff)) << 10U);
        }
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_ABS64_LO20:
    case R_LARCH_PCALA64_LO20:
    case R_LARCH_GOT64_PC_LO20:
    case R_LARCH_GOT64_LO20:
    case R_LARCH_TLS_LE64_LO20:
    case R_LARCH_TLS_IE64_PC_LO20:
    case R_LARCH_TLS_IE64_LO20:
        if ((instruction & UINT32_C(0xfe000000)) !=
            UINT32_C(0x16000000)) {
            return false;
        }
        if (reloc->type == R_LARCH_PCALA64_LO20 ||
            reloc->type == R_LARCH_GOT64_PC_LO20 ||
            reloc->type == R_LARCH_TLS_IE64_PC_LO20) {
            value = minild_loongarch_page_delta(target, place, reloc->type);
        }
        instruction = (instruction & ~UINT32_C(0x01ffffe0)) |
                      ((((uint32_t)(value >> 32U)) & UINT32_C(0xfffff)) << 5U);
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_ABS64_HI12:
    case R_LARCH_PCALA64_HI12:
    case R_LARCH_GOT64_PC_HI12:
    case R_LARCH_GOT64_HI12:
    case R_LARCH_TLS_LE64_HI12:
    case R_LARCH_TLS_IE64_PC_HI12:
    case R_LARCH_TLS_IE64_HI12:
        if ((instruction & UINT32_C(0xffc00000)) !=
            UINT32_C(0x03000000)) {
            return false;
        }
        if (reloc->type == R_LARCH_PCALA64_HI12 ||
            reloc->type == R_LARCH_GOT64_PC_HI12 ||
            reloc->type == R_LARCH_TLS_IE64_PC_HI12) {
            value = minild_loongarch_page_delta(target, place, reloc->type);
        }
        instruction = (instruction & ~UINT32_C(0x003ffc00)) |
                      ((((uint32_t)(value >> 52U)) & UINT32_C(0xfff)) << 10U);
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_TLS_TPREL32:
        if (!minild_nonriscv_signed_fits(target, 32U)) {
            return false;
        }
        store_u32le(slot, (uint32_t)target);
        return true;
    case R_LARCH_TLS_TPREL64:
        store_u64le(slot, target);
        return true;
    case R_LARCH_TLS_LE_ADD_R:
        /* Hint on the existing TP addition; no opcode rewrite is needed. */
        return true;
    case R_LARCH_ADD6:
    case R_LARCH_SUB6:
    case R_LARCH_ADD8:
    case R_LARCH_ADD16:
    case R_LARCH_ADD24:
    case R_LARCH_ADD32:
    case R_LARCH_ADD64:
    case R_LARCH_SUB8:
    case R_LARCH_SUB16:
    case R_LARCH_SUB24:
    case R_LARCH_SUB32:
    case R_LARCH_SUB64:
        sub = reloc->type == R_LARCH_SUB6 ||
              reloc->type == R_LARCH_SUB8 ||
              reloc->type == R_LARCH_SUB16 ||
              reloc->type == R_LARCH_SUB24 ||
              reloc->type == R_LARCH_SUB32 ||
              reloc->type == R_LARCH_SUB64;
        width = minild_loongarch_static_instruction_width(reloc->type);
        if (reloc->type == R_LARCH_ADD6 || reloc->type == R_LARCH_SUB6) {
            unsigned char old_byte = slot[0];
            slot[0] = (unsigned char)((old_byte & UINT8_C(0xc0)) |
                      ((sub ? old_byte - target : old_byte + target) &
                       UINT64_C(0x3f)));
            return true;
        }
        value = 0U;
        for (i = 0U; i < width; ++i) {
            value |= (uint64_t)slot[i] << (i * 8U);
        }
        value = sub ? value - target : value + target;
        for (i = 0U; i < width; ++i) {
            slot[i] = (unsigned char)(value >> (i * 8U));
        }
        return true;
    default:
        return false;
    }
}

static bool minild_loongarch_static_validate_aligns(
    MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        uint64_t alignment;
        uint64_t bytes;
        uint64_t place;
        size_t off;
        size_t j;

        if (reloc->type != R_LARCH_ALIGN) {
            continue;
        }
        if (reloc->section >= state->work.count ||
            (layout->scripted && layout->work[reloc->section].discarded)) {
            continue;
        }
        section = &state->work.sections[reloc->section];
        if (section->type != SHT_PROGBITS ||
            (section->flags & SHF_EXECINSTR) == 0U ||
            reloc->addend < 0 || reloc->offset > SIZE_MAX) {
            goto invalid_align;
        }
        if (reloc->symbol != SIZE_MAX) {
            unsigned exponent = (unsigned)((uint64_t)reloc->addend & 0xffU);
            uint64_t skip_limit = (uint64_t)reloc->addend >> 8U;

            if (exponent < 2U || exponent > 20U) {
                goto invalid_align;
            }
            alignment = UINT64_C(1) << exponent;
            bytes = alignment - 4U;
            /* Max-skip directives cannot retain all of their temporary
             * reserve bytes: GNU's relaxation must remove them. */
            if (skip_limit != 0U && bytes > skip_limit) {
                goto invalid_align;
            }
        } else {
            bytes = (uint64_t)reloc->addend;
            if (bytes > UINT64_C(0x100000) || (bytes & 3U) != 0U) {
                goto invalid_align;
            }
            alignment = 4U;
            while (alignment <= bytes) {
                alignment <<= 1U;
            }
        }
        off = (size_t)reloc->offset;
        if (bytes > SIZE_MAX || !range_ok(off, (size_t)bytes, section->size) ||
            layout->work[reloc->section].vaddr > UINT64_MAX - reloc->offset ||
            (place = layout->work[reloc->section].vaddr + reloc->offset) >
                UINT64_MAX - bytes) {
            goto invalid_align;
        }
        for (j = 0U; j < (size_t)bytes; j += 4U) {
            if (load_u32le(section->data + off + j) !=
                UINT32_C(0x03400000)) {
                goto invalid_align;
            }
        }
        if ((place + bytes) % alignment != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-needs-relaxation:%s:%zu"
                    ":alignment=%llu:reserved=%llu\n",
                    section->name, off, (unsigned long long)alignment,
                    (unsigned long long)bytes);
            return false;
        }
        continue;

    invalid_align:
        fprintf(state->diagnostics,
                "minic-ld: invalid-loongarch-static-align:%zu\n", i);
        return false;
    }
    return true;
}

static bool minild_loongarch_static_instruction_sites_valid(
    MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdNonRiscvFixup kind =
            minild_loongarch_static_fixup_class(reloc->type);
        MiniLdSection *section;

        if (kind != MINILD_NONRISCV_TARGET_INSTRUCTION ||
            reloc->type == R_LARCH_TLS_TPREL32 ||
            reloc->type == R_LARCH_TLS_TPREL64 ||
            (reloc->type >= R_LARCH_ADD8 &&
             reloc->type <= R_LARCH_SUB64) ||
            reloc->type == R_LARCH_ADD6 ||
            reloc->type == R_LARCH_SUB6) {
            continue;
        }
        if (reloc->section >= state->work.count ||
            (layout->scripted && layout->work[reloc->section].discarded)) {
            continue;
        }
        section = &state->work.sections[reloc->section];
        if ((section->flags & SHF_EXECINSTR) == 0U ||
            section->type != SHT_PROGBITS) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-instruction-reloc-in-nonexec:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
    }
    return true;
}

static bool minild_loongarch_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_loongarch_static_fixup_class,
        minild_loongarch_static_resolve_target,
        minild_loongarch_static_instruction_width,
        minild_loongarch_static_write_instruction
    };

    return minild_loongarch_static_validate_aligns(state, layout) &&
           minild_loongarch_static_instruction_sites_valid(state, layout) &&
           minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
