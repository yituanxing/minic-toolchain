/* x86-64 ELF64 ET_EXEC psABI relocations. The GOTPCRELX relocation is
 * always a four-byte G+A-P displacement. Relaxation to S+A-P is an optional
 * target-owned opcode rewrite, never a prerequisite for a valid relocation. */

static MiniLdNonRiscvFixup minild_x86_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_X86_64_NONE: return MINILD_NONRISCV_NONE;
    case R_X86_64_64: return MINILD_NONRISCV_ABS64;
    case R_X86_64_32: return MINILD_NONRISCV_ABS32;
    case R_X86_64_32S: return MINILD_NONRISCV_ABS32_SIGNED;
    case R_X86_64_PC32:
    case R_X86_64_PLT32: return MINILD_NONRISCV_PCREL32;
    case R_X86_64_GOTPCREL: return MINILD_NONRISCV_PCREL32;
    case R_X86_64_GOTTPOFF: return MINILD_NONRISCV_PCREL32;
    case R_X86_64_TPOFF32:
    case R_X86_64_DTPOFF32: return MINILD_NONRISCV_ABS32_SIGNED;
    case R_X86_64_DTPOFF64: return MINILD_NONRISCV_ABS64;
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        return MINILD_NONRISCV_PCREL32;
    default: return MINILD_NONRISCV_INVALID;
    }
}

/* GNU AS emits GOTPCRELX on both relaxable and non-relaxable RIP-relative
 * operands. Only transform the encodings whose semantics are proven; every
 * other input retains the original machine instruction and uses a real GOT
 * slot. Linkers must not reject a valid relocation merely because a chosen
 * instruction isn't an optimization candidate (Ubuntu libc uses more forms
 * than Debian's startup fixture).
 *
 * The operand supplied by Common is S+A, and P is the displacement's own
 * address, four bytes before the next instruction.  Rewrite only when the
 * full S+A-P displacement is representable, preserving the section length. */
static bool minild_x86_try_relax_static_gotpcrelx(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdReloc *reloc, uint64_t target_bits) {
    MiniLdSection *source;
    unsigned char *slot;
    unsigned char modrm;
    uint64_t place;
    uint64_t delta;

    if (reloc->addend != -4 || reloc->section >= state->work.count ||
        reloc->offset < 2U || reloc->offset > SIZE_MAX ||
        layout->work[reloc->section].vaddr >
            UINT64_MAX - reloc->offset) {
        return false;
    }
    source = &state->work.sections[reloc->section];
    if (source->type == SHT_NOBITS ||
        !range_ok((size_t)reloc->offset, 4U, source->size)) {
        return false;
    }
    place = layout->work[reloc->section].vaddr + reloc->offset;
    delta = target_bits - place;
    if (!minild_nonriscv_signed_fits(delta, 32U)) {
        return false;
    }
    slot = source->data + (size_t)reloc->offset;
    modrm = slot[-1];
    if (reloc->type == R_X86_64_REX_GOTPCRELX) {
        if (reloc->offset < 3U || (slot[-3] & 0xf0U) != 0x40U ||
            slot[-2] != 0x8bU || (modrm & 0xc7U) != 0x05U) {
            return false;
        }
        slot[-2] = 0x8dU; /* MOV reg,[rip+GOT] -> LEA reg,[rip+sym]. */
        return true;
    }
    if (reloc->type == R_X86_64_GOTPCRELX) {
        if (slot[-2] == 0xffU &&
            (modrm == 0x15U || modrm == 0x25U)) {
            slot[-2] = 0x67U; /* Indirect CALL/JMP -> addr32 direct. */
            slot[-1] = modrm == 0x15U ? 0xe8U : 0xe9U;
            return true;
        }
        if (slot[-2] == 0x8bU && (modrm & 0xc7U) == 0x05U) {
            slot[-2] = 0x8dU;
            return true;
        }
    }
    return false;
}

static bool minild_x86_resolve_static_got_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target_bits) {
    const MiniLdX86StaticGot *got = plan->target_data;
    size_t slot;
    uint64_t got_va;
    uint64_t tls_offset;

    if (reloc->symbol != SIZE_MAX &&
        reloc->symbol < state->resolver.count &&
        ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
            STT_GNU_IFUNC &&
        (reloc->type == R_X86_64_PC32 ||
         reloc->type == R_X86_64_PLT32 ||
         reloc->type == R_X86_64_64 ||
         reloc->type == R_X86_64_32 ||
         reloc->type == R_X86_64_32S)) {
        size_t offset;

        if (got == NULL || reloc->symbol >= got->symbol_count ||
            got->ifunc_stub_section >= state->work.count ||
            (offset = got->ifunc_stub_offsets[reloc->symbol]) == SIZE_MAX ||
            offset > UINT64_MAX -
                         layout->work[got->ifunc_stub_section].vaddr) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-x86-static-ifunc-plt-relocation\n");
            return false;
        }
        *target_bits = layout->work[got->ifunc_stub_section].vaddr +
                       (uint64_t)offset + (uint64_t)reloc->addend;
        return true;
    }

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
    if (reloc->type == R_X86_64_TPOFF32 ||
        reloc->type == R_X86_64_DTPOFF32 ||
        reloc->type == R_X86_64_DTPOFF64) {
        if (got == NULL ||
            !minild_x86_static_tls_symbol_offset(state, layout, got,
                                                   reloc->symbol,
                                                   &tls_offset)) {
            return false;
        }
        if (reloc->type != R_X86_64_TPOFF32 &&
            !(state->resolver.entries[reloc->symbol].section ==
                  MINILD_SECTION_UNDEF &&
              ELF64_ST_BIND(state->resolver.entries[reloc->symbol].info) ==
                  STB_WEAK)) {
            tls_offset += got->tls_span;
        }
        *target_bits = tls_offset + (uint64_t)reloc->addend;
        return true;
    }
    if (reloc->type != R_X86_64_GOTPCREL &&
        reloc->type != R_X86_64_GOTPCRELX &&
        reloc->type != R_X86_64_REX_GOTPCRELX &&
        reloc->type != R_X86_64_GOTTPOFF) {
        return true;
    }
    if ((reloc->type == R_X86_64_GOTPCRELX ||
         reloc->type == R_X86_64_REX_GOTPCRELX) &&
        reloc->symbol != SIZE_MAX &&
        reloc->symbol < state->resolver.count &&
        !(state->resolver.entries[reloc->symbol].section ==
              MINILD_SECTION_UNDEF &&
          ELF64_ST_BIND(state->resolver.entries[reloc->symbol].info) ==
              STB_WEAK) &&
        minild_x86_try_relax_static_gotpcrelx(
            state, layout, reloc, *target_bits)) {
        return true;
    }
    if (got != NULL && reloc->type == R_X86_64_GOTTPOFF) {
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            got->tls_slot_offsets[reloc->symbol] == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-x86-static-tls-got-relocation\n");
            return false;
        }
        slot = got->tls_slot_offsets[reloc->symbol];
    } else if (got != NULL && reloc->symbol != SIZE_MAX &&
               reloc->symbol < got->symbol_count) {
        slot = got->slot_offsets[reloc->symbol];
    } else {
        slot = SIZE_MAX;
    }
    if (got == NULL || !got->present || layout == NULL ||
        layout->work == NULL || got->section >= state->work.count ||
        slot == SIZE_MAX ||
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

static bool minild_x86_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_x86_static_fixup_class,
        minild_x86_resolve_static_got_target,
        NULL,
        NULL
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
