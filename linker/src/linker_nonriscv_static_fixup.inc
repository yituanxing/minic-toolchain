/* One ELF64 scalar fixup writer and ET_EXEC traversal for x86-64, AArch64
 * and LoongArch64. Each machine supplies its own immutable relocation
 * classification and instruction writer in its Target leaf. A new target
 * never edits this loop or a different target's opcode/psABI switch. */

typedef enum MiniLdNonRiscvFixup {
    MINILD_NONRISCV_INVALID,
    MINILD_NONRISCV_NONE,
    MINILD_NONRISCV_ABS8,
    MINILD_NONRISCV_ABS16,
    MINILD_NONRISCV_ABS64,
    MINILD_NONRISCV_ABS32,
    MINILD_NONRISCV_ABS32_SIGNED,
    MINILD_NONRISCV_PCREL64,
    MINILD_NONRISCV_PCREL32,
    MINILD_NONRISCV_PCREL16,
    MINILD_NONRISCV_PCREL8,
    MINILD_NONRISCV_TARGET_INSTRUCTION
} MiniLdNonRiscvFixup;

typedef struct MiniLdNonRiscvStaticOps {
    MiniLdNonRiscvFixup (*classify)(uint32_t type);
    /* Target-only address transformations, e.g. x86 GOTPCREL G+A rather
     * than the ordinary symbol S+A.  Common still owns relocation walking
     * and scalar displacement encoding. */
    bool (*resolve_target)(MiniLdState *, const MiniLdLayout *,
                           const MiniLdStaticTargetPlan *,
                           const MiniLdReloc *, uint64_t *);
    /* Only instruction encodings reach this hook; scalar writes are Common. */
    size_t (*instruction_width)(uint32_t type);
    bool (*write_instruction)(const MiniLdState *state,
                              unsigned char *slot,
                              const MiniLdReloc *reloc,
                              uint64_t target, uint64_t place,
                              bool undefined_weak);
    /* A target may preserve the original bytes of an allocated runtime
     * relocation whose fully resolved RELA record was already emitted by
     * its post-layout writer (e.g. AArch64 --no-apply-dynamic-relocs). */
    bool (*defer_fixup)(const MiniLdState *, const MiniLdReloc *);
} MiniLdNonRiscvStaticOps;

static size_t minild_nonriscv_word_instruction_width(uint32_t type) {
    (void)type;
    return 4U;
}

/* Treat ELF S+A and S+A-P as 64-bit relocation bit patterns; this avoids
 * signed overflow and preserves high-half kernel addresses.  Narrow writes
 * must be exactly representable after the psABI's sign/zero extension. */
static bool minild_nonriscv_signed_fits(uint64_t bits, unsigned width) {
    uint64_t sign_bit = UINT64_C(1) << (width - 1U);

    return bits < sign_bit || bits >= UINT64_MAX - (sign_bit - 1U);
}

static bool minild_nonriscv_write_fixup(const MiniLdState *state,
                                        MiniLdSection *section,
                                        const MiniLdReloc *reloc,
                                        uint16_t machine,
                                        MiniLdNonRiscvFixup kind,
                                        const MiniLdNonRiscvStaticOps *ops,
                                        uint64_t target,
                                        uint64_t place,
                                        bool undefined_weak,
                                        FILE *diagnostics) {
    size_t width =
        kind == MINILD_NONRISCV_ABS8 ||
        kind == MINILD_NONRISCV_PCREL8 ? 1U :
        kind == MINILD_NONRISCV_ABS16 ||
        kind == MINILD_NONRISCV_PCREL16 ? 2U :
        kind == MINILD_NONRISCV_ABS64 ||
        kind == MINILD_NONRISCV_PCREL64 ? 8U : 4U;
    unsigned char *slot;
    uint64_t delta = target - place;

    if (kind == MINILD_NONRISCV_TARGET_INSTRUCTION) {
        if (ops->instruction_width == NULL ||
            (width = ops->instruction_width(reloc->type)) == 0U) {
            fprintf(diagnostics,
                    "minic-ld: nonriscv-invalid-instruction-width:%u:%u\n",
                    (unsigned)machine, (unsigned)reloc->type);
            return false;
        }
    }
    if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
        !range_ok((size_t)reloc->offset, width, section->size)) {
        fprintf(diagnostics,
                "minic-ld: nonriscv-relocation-offset-out-of-range:%u:%u\n",
                (unsigned)machine, (unsigned)reloc->type);
        return false;
    }
    slot = section->data + (size_t)reloc->offset;

    switch (kind) {
    case MINILD_NONRISCV_ABS8:
        if (target > UINT8_MAX) {
            break;
        }
        store_u8(slot, (uint8_t)target);
        return true;
    case MINILD_NONRISCV_ABS16:
        if (target > UINT16_MAX) {
            break;
        }
        store_u16le(slot, (uint16_t)target);
        return true;
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
    case MINILD_NONRISCV_PCREL16:
        if (!minild_nonriscv_signed_fits(delta, 16U)) {
            break;
        }
        store_u16le(slot, (uint16_t)delta);
        return true;
    case MINILD_NONRISCV_PCREL8:
        if (!minild_nonriscv_signed_fits(delta, 8U)) {
            break;
        }
        store_u8(slot, (uint8_t)delta);
        return true;
    case MINILD_NONRISCV_TARGET_INSTRUCTION:
        if (ops->write_instruction != NULL &&
            ops->write_instruction(state, slot, reloc, target, place,
                                   undefined_weak)) {
            return true;
        }
        break;
    default:
        break;
    }
    fprintf(diagnostics,
            "minic-ld: nonriscv-relocation-overflow-or-encoding:%u:%u\n",
            (unsigned)machine, (unsigned)reloc->type);
    return false;
}

static bool minild_nonriscv_apply_with_ops(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan,
    const MiniLdNonRiscvStaticOps *ops) {
    uint16_t machine;
    size_t i;

    if (state == NULL || state->target == NULL || layout == NULL ||
        layout->work == NULL || ops == NULL || ops->classify == NULL) {
        return false;
    }
    machine = state->target->machine;
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdNonRiscvFixup kind = ops->classify(reloc->type);
        MiniLdSection *section;
        MiniLdStaticRelocOperands operands;
        bool undefined_weak = false;
        bool live;

        if (kind == MINILD_NONRISCV_INVALID) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-static-relocation:%u:%u\n",
                    (unsigned)machine, (unsigned)reloc->type);
            return false;
        }
        if (kind == MINILD_NONRISCV_NONE) {
            continue;
        }
        if (!minild_static_reloc_source(
                state, layout, reloc,
                "nonriscv-invalid-relocation-section", &section, &live)) {
            return false;
        }
        if (!live) {
            continue;
        }
        if (!minild_static_reloc_operands(
                state, layout, reloc, "nonriscv-relocation-place-overflow",
                &operands)) {
            return false;
        }
        if (ops->resolve_target != NULL &&
            !ops->resolve_target(state, layout, plan, reloc,
                                 &operands.target_bits)) {
            return false;
        }
        if (reloc->symbol != SIZE_MAX &&
            reloc->symbol < state->resolver.count) {
            const MiniLdResolvedSymbol *symbol =
                &state->resolver.entries[reloc->symbol];
            undefined_weak = symbol->section == MINILD_SECTION_UNDEF &&
                ELF64_ST_BIND(symbol->info) == STB_WEAK;
        }
        if (ops->defer_fixup != NULL &&
            ops->defer_fixup(state, reloc)) {
            continue;
        }
        if (!minild_nonriscv_write_fixup(state, section, reloc, machine, kind, ops,
                                         operands.target_bits, operands.place,
                                         undefined_weak,
                                         state->diagnostics)) {
            return false;
        }
    }
    return true;
}
