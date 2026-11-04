/* x86-64 ELF64 ET_EXEC psABI relocations. No x86 machine instruction
 * encoder is needed for the currently admitted scalar/PC-relative forms. */

static MiniLdNonRiscvFixup minild_x86_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_X86_64_NONE: return MINILD_NONRISCV_NONE;
    case R_X86_64_64: return MINILD_NONRISCV_ABS64;
    case R_X86_64_32: return MINILD_NONRISCV_ABS32;
    case R_X86_64_32S: return MINILD_NONRISCV_ABS32_SIGNED;
    case R_X86_64_PC32:
    case R_X86_64_PLT32: return MINILD_NONRISCV_PCREL32;
    case R_X86_64_GOTPCREL: return MINILD_NONRISCV_PCREL32;
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        return MINILD_NONRISCV_TARGET_INSTRUCTION;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static bool minild_x86_resolve_static_got_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target_bits) {
    const MiniLdX86StaticGot *got = plan->target_data;
    size_t slot;
    uint64_t got_va;

    if (reloc->symbol != SIZE_MAX &&
        reloc->symbol < state->resolver.count &&
        ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
            STT_GNU_IFUNC &&
        (reloc->type == R_X86_64_GOTPCREL ||
         reloc->type == R_X86_64_GOTPCRELX ||
         reloc->type == R_X86_64_REX_GOTPCRELX)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-x86-static-ifunc-got-relocation\n");
        return false;
    }
    if (reloc->type != R_X86_64_GOTPCREL) {
        return true;
    }
    if (got == NULL || !got->present || layout == NULL ||
        layout->work == NULL || got->section >= state->work.count ||
        reloc->symbol == SIZE_MAX || reloc->symbol >= got->symbol_count ||
        (slot = got->slot_offsets[reloc->symbol]) == SIZE_MAX ||
        (uint64_t)slot > UINT64_MAX -
                             layout->work[got->section].vaddr) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-x86-static-got-relocation\n");
        return false;
    }
    got_va = layout->work[got->section].vaddr + (uint64_t)slot;
    *target_bits = got_va + (uint64_t)reloc->addend;
    return true;
}

/* In a non-preemptible static image the x86 psABI's relaxable GOT forms
 * can refer directly to the symbol.  Verify the opcode before rewriting;
 * an unrecognized instruction must never silently receive an S+A-P fixup.
 * The displacement always starts four bytes before the next RIP. */
static size_t minild_x86_static_instruction_width(uint32_t type) {
    return type == R_X86_64_GOTPCRELX ||
                   type == R_X86_64_REX_GOTPCRELX ? 4U : 0U;
}

static bool minild_x86_static_write_instruction(
    unsigned char *slot, const MiniLdReloc *reloc,
    uint64_t target, uint64_t place, bool undefined_weak) {
    uint64_t delta = target - place;
    unsigned char modrm;

    if (reloc->addend != -4 || reloc->offset < 2U) {
        return false;
    }
    modrm = slot[-1];
    if (reloc->type == R_X86_64_REX_GOTPCRELX) {
        unsigned char rex;

        if (reloc->offset < 3U) {
            return false;
        }
        rex = slot[-3];
        if ((rex & 0xf0U) != 0x40U || (modrm & 0xc7U) != 0x05U ||
            slot[-2] != 0x8bU) {
            return false;
        }
        if (undefined_weak) {
            /* An unresolved weak symbol must evaluate to zero, not the
             * address of a synthesized GOT entry or an out-of-range LEA. */
            if ((rex & 0x01U) != 0U) {
                return false;
            }
            slot[-2] = 0xc7U;
            slot[-1] = (unsigned char)(0xc0U | ((modrm >> 3U) & 7U));
            store_u32le(slot, 0U);
            return true;
        }
        if (!minild_nonriscv_signed_fits(delta, 32U)) {
            return false;
        }
        slot[-2] = 0x8dU; /* MOV reg,[rip+GOT] -> LEA reg,[rip+sym] */
        store_u32le(slot, (uint32_t)delta);
        return true;
    }
    if (reloc->type == R_X86_64_GOTPCRELX) {
        if (undefined_weak && slot[-2] == 0x8bU &&
            (modrm & 0xc7U) == 0x05U) {
            slot[-2] = 0xc7U; /* MOV r32,0; zero extends to r64. */
            slot[-1] = (unsigned char)(0xc0U | ((modrm >> 3U) & 7U));
            store_u32le(slot, 0U);
            return true;
        }
        if (slot[-2] == 0xffU &&
            !undefined_weak &&
            (modrm == 0x15U || modrm == 0x25U) &&
            minild_nonriscv_signed_fits(delta, 32U)) {
            slot[-2] = 0x67U; /* 6-byte indirect CALL/JMP -> addr32 CALL/JMP */
            slot[-1] = modrm == 0x15U ? 0xe8U : 0xe9U;
            store_u32le(slot, (uint32_t)delta);
            return true;
        }
        if (slot[-2] == 0x8bU && (modrm & 0xc7U) == 0x05U &&
            minild_nonriscv_signed_fits(delta, 32U)) {
            slot[-2] = 0x8dU;
            store_u32le(slot, (uint32_t)delta);
            return true;
        }
    }
    return false;
}

static bool minild_x86_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_x86_static_fixup_class,
        minild_x86_resolve_static_got_target,
        minild_x86_static_instruction_width,
        minild_x86_static_write_instruction
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
