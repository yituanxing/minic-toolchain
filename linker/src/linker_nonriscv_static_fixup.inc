/* ELF64 non-RISC-V ET_EXEC relocation policy.  This is deliberately a target
 * leaf: admission, canonical symbols, output layout and file emission remain
 * owned by the common linker.  ET_DYN, GOT/PLT and TLS have separate mode
 * contracts and are not inferred from these static relocation handlers. */

typedef enum MiniLdNonRiscvFixup {
    MINILD_NONRISCV_INVALID,
    MINILD_NONRISCV_NONE,
    MINILD_NONRISCV_ABS64,
    MINILD_NONRISCV_ABS32,
    MINILD_NONRISCV_ABS32_SIGNED,
    MINILD_NONRISCV_PCREL64,
    MINILD_NONRISCV_PCREL32,
    MINILD_NONRISCV_A64_BRANCH26,
    MINILD_NONRISCV_A64_ADRP21,
    MINILD_NONRISCV_A64_ADD_LO12,
    MINILD_NONRISCV_A64_LDST8_LO12,
    MINILD_NONRISCV_A64_LDST16_LO12,
    MINILD_NONRISCV_A64_LDST32_LO12,
    MINILD_NONRISCV_A64_LDST64_LO12,
    MINILD_NONRISCV_A64_LDST128_LO12,
    MINILD_NONRISCV_LA_BRANCH26,
    MINILD_NONRISCV_LA_ABS_HI20,
    MINILD_NONRISCV_LA_ABS_LO12,
    MINILD_NONRISCV_LA_PCALA_HI20,
    MINILD_NONRISCV_LA_PCALA_LO12
} MiniLdNonRiscvFixup;

static MiniLdNonRiscvFixup minild_nonriscv_fixup_class(uint16_t machine,
                                                       uint32_t type) {
    switch (machine) {
    case EM_X86_64:
        switch (type) {
        case R_X86_64_NONE: return MINILD_NONRISCV_NONE;
        case R_X86_64_64: return MINILD_NONRISCV_ABS64;
        case R_X86_64_32: return MINILD_NONRISCV_ABS32;
        case R_X86_64_32S: return MINILD_NONRISCV_ABS32_SIGNED;
        case R_X86_64_PC32:
        case R_X86_64_PLT32: return MINILD_NONRISCV_PCREL32;
        default: return MINILD_NONRISCV_INVALID;
        }
    case EM_AARCH64:
        switch (type) {
        case R_AARCH64_NONE: return MINILD_NONRISCV_NONE;
        case R_AARCH64_ABS64: return MINILD_NONRISCV_ABS64;
        case R_AARCH64_ABS32: return MINILD_NONRISCV_ABS32;
        case R_AARCH64_PREL64: return MINILD_NONRISCV_PCREL64;
        case R_AARCH64_PREL32: return MINILD_NONRISCV_PCREL32;
        case R_AARCH64_CALL26:
        case R_AARCH64_JUMP26: return MINILD_NONRISCV_A64_BRANCH26;
        case R_AARCH64_ADR_PREL_PG_HI21:
            return MINILD_NONRISCV_A64_ADRP21;
        case R_AARCH64_ADD_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_ADD_LO12;
        case R_AARCH64_LDST8_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_LDST8_LO12;
        case R_AARCH64_LDST16_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_LDST16_LO12;
        case R_AARCH64_LDST32_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_LDST32_LO12;
        case R_AARCH64_LDST64_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_LDST64_LO12;
        case R_AARCH64_LDST128_ABS_LO12_NC:
            return MINILD_NONRISCV_A64_LDST128_LO12;
        default: return MINILD_NONRISCV_INVALID;
        }
    case EM_LOONGARCH:
        switch (type) {
        case R_LARCH_NONE:
        case R_LARCH_MARK_LA:
        case R_LARCH_MARK_PCREL:
        case R_LARCH_RELAX: return MINILD_NONRISCV_NONE;
        case R_LARCH_64: return MINILD_NONRISCV_ABS64;
        case R_LARCH_32: return MINILD_NONRISCV_ABS32;
        case R_LARCH_64_PCREL: return MINILD_NONRISCV_PCREL64;
        case R_LARCH_32_PCREL: return MINILD_NONRISCV_PCREL32;
        case R_LARCH_B26: return MINILD_NONRISCV_LA_BRANCH26;
        case R_LARCH_ABS_HI20: return MINILD_NONRISCV_LA_ABS_HI20;
        case R_LARCH_ABS_LO12: return MINILD_NONRISCV_LA_ABS_LO12;
        case R_LARCH_PCALA_HI20: return MINILD_NONRISCV_LA_PCALA_HI20;
        case R_LARCH_PCALA_LO12: return MINILD_NONRISCV_LA_PCALA_LO12;
        default: return MINILD_NONRISCV_INVALID;
        }
    default:
        return MINILD_NONRISCV_INVALID;
    }
}

/* Treat ELF S+A and S+A-P as 64-bit relocation bit patterns; this avoids
 * signed overflow and preserves high-half kernel addresses.  Narrow writes
 * must be exactly representable after the psABI's sign/zero extension. */
static bool minild_nonriscv_signed_fits(uint64_t bits, unsigned width) {
    uint64_t sign_bit = UINT64_C(1) << (width - 1U);

    return bits < sign_bit || bits >= UINT64_MAX - (sign_bit - 1U);
}

static bool minild_nonriscv_write_fixup(MiniLdSection *section,
                                        const MiniLdReloc *reloc,
                                        uint16_t machine,
                                        MiniLdNonRiscvFixup kind,
                                        uint64_t target,
                                        uint64_t place,
                                        FILE *diagnostics) {
    size_t width = kind == MINILD_NONRISCV_ABS64 ||
                   kind == MINILD_NONRISCV_PCREL64 ? 8U : 4U;
    unsigned char *slot;
    uint64_t delta = target - place;
    uint32_t instruction;
    uint32_t imm;

    if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
        !range_ok((size_t)reloc->offset, width, section->size)) {
        fprintf(diagnostics,
                "minic-ld: nonriscv-relocation-offset-out-of-range:%u:%u\n",
                (unsigned)machine, (unsigned)reloc->type);
        return false;
    }
    slot = section->data + (size_t)reloc->offset;

    switch (kind) {
    case MINILD_NONRISCV_ABS64:
        store_u64le(slot, target);
        return true;
    case MINILD_NONRISCV_ABS32:
        if (target > UINT32_MAX) {
            break;
        }
        store_u32le(slot, (uint32_t)target);
        return true;
    case MINILD_NONRISCV_ABS32_SIGNED:
        if (!minild_nonriscv_signed_fits(target, 32U)) {
            break;
        }
        store_u32le(slot, (uint32_t)target);
        return true;
    case MINILD_NONRISCV_PCREL64:
        store_u64le(slot, delta);
        return true;
    case MINILD_NONRISCV_PCREL32:
        if (!minild_nonriscv_signed_fits(delta, 32U)) {
            break;
        }
        store_u32le(slot, (uint32_t)delta);
        return true;
    case MINILD_NONRISCV_A64_BRANCH26:
        if ((delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(delta, 28U)) {
            break;
        }
        instruction = load_u32le(slot);
        if ((reloc->type == R_AARCH64_CALL26 &&
             (instruction & UINT32_C(0xfc000000)) != UINT32_C(0x94000000)) ||
            (reloc->type == R_AARCH64_JUMP26 &&
             (instruction & UINT32_C(0xfc000000)) != UINT32_C(0x14000000))) {
            break;
        }
        store_u32le(slot, (instruction & UINT32_C(0xfc000000)) |
                           (((uint32_t)delta >> 2U) & UINT32_C(0x03ffffff)));
        return true;
    case MINILD_NONRISCV_A64_ADRP21: {
        uint64_t page_delta = (target >> 12U) - (place >> 12U);

        if (!minild_nonriscv_signed_fits(page_delta, 21U)) {
            break;
        }
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x9f000000)) != UINT32_C(0x90000000)) {
            break;
        }
        imm = (uint32_t)page_delta & UINT32_C(0x1fffff);
        instruction &= ~UINT32_C(0x60ffffe0);
        instruction |= (imm & 3U) << 29U;
        instruction |= ((imm >> 2U) & UINT32_C(0x7ffff)) << 5U;
        store_u32le(slot, instruction);
        return true;
    }
    case MINILD_NONRISCV_A64_ADD_LO12:
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x7f400000)) != UINT32_C(0x11000000)) {
            break;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((uint32_t)target & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case MINILD_NONRISCV_A64_LDST8_LO12:
    case MINILD_NONRISCV_A64_LDST16_LO12:
    case MINILD_NONRISCV_A64_LDST32_LO12:
    case MINILD_NONRISCV_A64_LDST64_LO12:
    case MINILD_NONRISCV_A64_LDST128_LO12: {
        unsigned shift = kind == MINILD_NONRISCV_A64_LDST8_LO12 ? 0U :
                         kind == MINILD_NONRISCV_A64_LDST16_LO12 ? 1U :
                         kind == MINILD_NONRISCV_A64_LDST32_LO12 ? 2U :
                         kind == MINILD_NONRISCV_A64_LDST64_LO12 ? 3U : 4U;
        uint32_t mask = (UINT32_C(1) << shift) - 1U;

        /* Scaled unsigned load/store immediates must retain alignment. */
        if (((uint32_t)target & mask) != 0U) {
            break;
        }
        instruction = load_u32le(slot);
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((((uint32_t)target & UINT32_C(0xfff)) >> shift) &
                        UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    }
    case MINILD_NONRISCV_LA_BRANCH26:
        if ((delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(delta, 28U)) {
            break;
        }
        instruction = load_u32le(slot) & ~UINT32_C(0x03ffffff);
        instruction |= ((uint32_t)delta >> 18U) & UINT32_C(0x3ff);
        instruction |= (((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case MINILD_NONRISCV_LA_ABS_HI20:
    case MINILD_NONRISCV_LA_PCALA_HI20: {
        /* Current LoongArch psABI rounds the PCALA page for the signed
         * low-12 addi.d, unlike the older unrounded ABI formula. */
        uint64_t page_delta = kind == MINILD_NONRISCV_LA_PCALA_HI20
                                  ? ((target + UINT64_C(0x800)) >> 12U) -
                                        (place >> 12U)
                                  : target >> 12U;

        if ((kind == MINILD_NONRISCV_LA_PCALA_HI20 &&
             !minild_nonriscv_signed_fits(page_delta, 20U)) ||
            (kind == MINILD_NONRISCV_LA_ABS_HI20 &&
             target > UINT32_MAX &&
             !minild_nonriscv_signed_fits(target, 32U))) {
            break;
        }
        imm = (uint32_t)page_delta & UINT32_C(0xfffff);
        instruction = load_u32le(slot);
        instruction &= ~UINT32_C(0x01ffffe0);
        instruction |= imm << 5U;
        store_u32le(slot, instruction);
        return true;
    }
    case MINILD_NONRISCV_LA_ABS_LO12:
    case MINILD_NONRISCV_LA_PCALA_LO12:
        instruction = load_u32le(slot);
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((uint32_t)target & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    default:
        break;
    }
    fprintf(diagnostics,
            "minic-ld: nonriscv-relocation-overflow-or-encoding:%u:%u\n",
            (unsigned)machine, (unsigned)reloc->type);
    return false;
}

static bool minild_nonriscv_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    uint16_t machine;
    size_t i;

    (void)plan;
    if (state == NULL || state->target == NULL || layout == NULL ||
        layout->work == NULL) {
        return false;
    }
    machine = state->target->machine;
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdNonRiscvFixup kind =
            minild_nonriscv_fixup_class(machine, reloc->type);
        MiniLdSection *section;
        uint64_t value = 0U;
        uint64_t target;
        uint64_t place;

        if (kind == MINILD_NONRISCV_INVALID) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-static-relocation:%u:%u\n",
                    (unsigned)machine, (unsigned)reloc->type);
            return false;
        }
        if (kind == MINILD_NONRISCV_NONE) {
            continue;
        }
        if (reloc->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: nonriscv-invalid-relocation-section\n");
            return false;
        }
        section = &state->work.sections[reloc->section];
        if (layout->scripted &&
            ((section->flags & SHF_ALLOC) == 0U ||
             layout->work[reloc->section].discarded)) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX &&
            !minild_layout_resolve_symbol(state, layout,
                                         reloc->symbol, &value)) {
            return false;
        }
        if (reloc->offset > UINT64_MAX - layout->work[reloc->section].vaddr) {
            fprintf(state->diagnostics,
                    "minic-ld: nonriscv-relocation-place-overflow\n");
            return false;
        }
        target = value + (uint64_t)reloc->addend;
        place = layout->work[reloc->section].vaddr + reloc->offset;
        if (!minild_nonriscv_write_fixup(section, reloc, machine, kind,
                                         target, place, state->diagnostics)) {
            return false;
        }
    }
    return true;
}
