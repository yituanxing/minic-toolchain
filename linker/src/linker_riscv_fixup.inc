/* RV64 ELF target relocation encoding and HI20/LO12 pairing primitives.
 * Included once by linker_layout.inc after the byte load/store accessors;
 * static and shared final-link consumers use the same checked encoders.
 */

typedef struct MiniLdPcrelSlot {
    size_t section_plus_one;
    uint64_t offset;
    int64_t delta;
} MiniLdPcrelSlot;

static int64_t riscv_hi20(int64_t value) {
    int64_t high = value / INT64_C(4096);
    int64_t low = value % INT64_C(4096);

    /* Signed division truncates toward zero.  Explicitly round to the
     * canonical signed LO12 interval without overflowing value+0x800,
     * shifting a negative signed value, or relying on signed right shift. */
    if (low >= INT64_C(2048)) {
        ++high;
    } else if (low < -INT64_C(2048)) {
        --high;
    }
    return high;
}

static int64_t riscv_lo12(int64_t value) {
    int64_t low = value % INT64_C(4096);

    if (low >= INT64_C(2048)) {
        low -= INT64_C(4096);
    } else if (low < -INT64_C(2048)) {
        low += INT64_C(4096);
    }
    return low;
}

static bool static_patch_utype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm20,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((uint32_t)imm20 & UINT32_C(0x000fffff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_itype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x000fffff);
    instruction |= ((uint32_t)imm12 & UINT32_C(0x00000fff)) << 20U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_stype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded = (uint32_t)imm12 & UINT32_C(0x00000fff);

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 5U) & UINT32_C(0x7f)) << 25U;
    instruction |= (encoded & UINT32_C(0x1f)) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_branch(MiniLdSection *section,
                                uint64_t offset,
                                int64_t delta,
                                FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -4096 || delta > 4094) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 12U) & 1U) << 31U;
    instruction |= ((encoded >> 5U) & UINT32_C(0x3f)) << 25U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x0f)) << 8U;
    instruction |= ((encoded >> 11U) & 1U) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}


static bool static_patch_rvc_branch(MiniLdSection *section,
                                    uint64_t offset,
                                    int64_t delta,
                                    FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -256 || delta > 254) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0x1ff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1c7c);
    instruction |= (uint16_t)(((encoded >> 8U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 3U) & 3U) << 10U);
    instruction |= (uint16_t)(((encoded >> 6U) & 3U) << 5U);
    instruction |= (uint16_t)(((encoded >> 1U) & 3U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_rvc_jump(MiniLdSection *section,
                                  uint64_t offset,
                                  int64_t delta,
                                  FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -2048 || delta > 2046) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_JUMP-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0xfff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1ffc);
    instruction |= (uint16_t)(((encoded >> 11U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 4U) & 1U) << 11U);
    instruction |= (uint16_t)(((encoded >> 8U) & 3U) << 9U);
    instruction |= (uint16_t)(((encoded >> 10U) & 1U) << 8U);
    instruction |= (uint16_t)(((encoded >> 6U) & 1U) << 7U);
    instruction |= (uint16_t)(((encoded >> 7U) & 1U) << 6U);
    instruction |= (uint16_t)(((encoded >> 1U) & 7U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_jal(MiniLdSection *section,
                             uint64_t offset,
                             int64_t delta,
                             FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -1048576 || delta > 1048574) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_JAL-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fffff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((encoded >> 20U) & 1U) << 31U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x03ff)) << 21U;
    instruction |= ((encoded >> 11U) & 1U) << 20U;
    instruction |= ((encoded >> 12U) & UINT32_C(0x00ff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static size_t pcrel_capacity_for(size_t relocation_count) {
    size_t capacity = 64U;
    size_t target;

    if (relocation_count > (SIZE_MAX - 1U) / 2U) {
        return 0U;
    }
    target = relocation_count * 2U + 1U;
    while (capacity < target) {
        if (capacity > SIZE_MAX / 2U) {
            return 0U;
        }
        capacity *= 2U;
    }
    return capacity;
}

static size_t pcrel_hash(size_t section, uint64_t offset, size_t capacity) {
    uint64_t hash = offset ^ (UINT64_C(0x9e3779b97f4a7c15) *
                              (uint64_t)(section + 1U));
    hash ^= hash >> 33U;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33U;
    return (size_t)hash & (capacity - 1U);
}

static void pcrel_insert(MiniLdPcrelSlot *slots,
                         size_t capacity,
                         size_t section,
                         uint64_t offset,
                         int64_t delta) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        position = (position + 1U) & (capacity - 1U);
    }
    slots[position].section_plus_one = section + 1U;
    slots[position].offset = offset;
    slots[position].delta = delta;
}

static bool pcrel_find(const MiniLdPcrelSlot *slots,
                       size_t capacity,
                       size_t section,
                       uint64_t offset,
                       int64_t *delta_out) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        if (slots[position].section_plus_one == section + 1U &&
            slots[position].offset == offset) {
            *delta_out = slots[position].delta;
            return true;
        }
        position = (position + 1U) & (capacity - 1U);
    }
    return false;
}

/* A target-level arithmetic fixup has the same byte semantics in ET_EXEC
 * and ET_DYN.  Input liveness and final S+A resolution still belong to the
 * respective output pipeline; only the checked RISC-V field write is shared.
 * Preserve the existing mode-specific diagnostic spellings for callers. */
static bool minild_riscv_is_arithmetic_relocation(uint32_t type) {
    switch (type) {
    case R_RISCV_ADD8:
    case R_RISCV_ADD16:
    case R_RISCV_ADD32:
    case R_RISCV_ADD64:
    case R_RISCV_SUB6:
    case R_RISCV_SUB8:
    case R_RISCV_SUB16:
    case R_RISCV_SUB32:
    case R_RISCV_SUB64:
    case R_RISCV_SET6:
    case R_RISCV_SET8:
    case R_RISCV_SET16:
    case R_RISCV_SET32:
        return true;
    default:
        return false;
    }
}

/* Only the RISC-V psABI type is classified here.  Whether a relocation
 * reserves a dynamic record, needs a PLT, or can be resolved without a
 * loader is an output-mode decision (especially under PIE/preemption).
 * The same shape classification feeds static and shared pre-layout scans. */
typedef enum MiniLdRiscvRelocKind {
    MINILD_RV_RELOC_NOOP,
    MINILD_RV_RELOC_ALIGN,
    MINILD_RV_RELOC_ABS32,
    MINILD_RV_RELOC_ABS64,
    MINILD_RV_RELOC_ARITHMETIC,
    MINILD_RV_RELOC_FINAL,
    MINILD_RV_RELOC_CALL,
    MINILD_RV_RELOC_GOT,
    MINILD_RV_RELOC_TLS_GD,
    MINILD_RV_RELOC_UNSUPPORTED
} MiniLdRiscvRelocKind;

static MiniLdRiscvRelocKind minild_riscv_reloc_kind(uint32_t type) {
    if (minild_riscv_is_arithmetic_relocation(type)) {
        return MINILD_RV_RELOC_ARITHMETIC;
    }
    switch (type) {
    case R_RISCV_NONE:
    case R_RISCV_RELAX:
        return MINILD_RV_RELOC_NOOP;
    case R_RISCV_ALIGN:
        return MINILD_RV_RELOC_ALIGN;
    case R_RISCV_32:
        return MINILD_RV_RELOC_ABS32;
    case R_RISCV_64:
        return MINILD_RV_RELOC_ABS64;
    case R_RISCV_PCREL_HI20:
    case R_RISCV_PCREL_LO12_I:
    case R_RISCV_PCREL_LO12_S:
    case R_RISCV_HI20:
    case R_RISCV_LO12_I:
    case R_RISCV_LO12_S:
    case R_RISCV_BRANCH:
    case R_RISCV_JAL:
    case R_RISCV_RVC_BRANCH:
    case R_RISCV_RVC_JUMP:
    case R_RISCV_32_PCREL:
        return MINILD_RV_RELOC_FINAL;
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT:
        return MINILD_RV_RELOC_CALL;
    case R_RISCV_GOT_HI20:
        return MINILD_RV_RELOC_GOT;
    case R_RISCV_TLS_GD_HI20:
        return MINILD_RV_RELOC_TLS_GD;
    default:
        return MINILD_RV_RELOC_UNSUPPORTED;
    }
}

/* Script input selection only needs to know whether a target relocation
 * carries no binding or reachability dependency.  Keep that ISA decision
 * here instead of spelling RISC-V relocation numbers in generic selection. */
static bool minild_target_ignored_script_reloc(uint32_t type) {
    MiniLdRiscvRelocKind kind = minild_riscv_reloc_kind(type);

    return kind == MINILD_RV_RELOC_NOOP || kind == MINILD_RV_RELOC_ALIGN;
}

static bool minild_riscv_reloc_final_layout(uint32_t type) {
    switch (minild_riscv_reloc_kind(type)) {
    case MINILD_RV_RELOC_ARITHMETIC:
    case MINILD_RV_RELOC_FINAL:
    case MINILD_RV_RELOC_GOT:
    case MINILD_RV_RELOC_TLS_GD:
        return true;
    default:
        return false;
    }
}

/* A final-link PCREL HI20/LO12 pairing table belongs to the RISC-V fixup
 * stage, not to either ET_EXEC or ET_DYN output driver.  The table is built
 * only after layout, when R_RISCV_ALIGN has settled all current work offsets.
 * Section identities are already stable at that point. */
typedef struct MiniLdRiscvPcrelPairs {
    MiniLdPcrelSlot *slots;
    size_t capacity;
} MiniLdRiscvPcrelPairs;

static bool minild_riscv_pairs_init(MiniLdRiscvPcrelPairs *pairs,
                                   size_t relocation_count,
                                   bool shared_output,
                                   FILE *diagnostics) {
    memset(pairs, 0, sizeof(*pairs));
    pairs->capacity = pcrel_capacity_for(relocation_count);
    if (pairs->capacity == 0U) {
        fprintf(diagnostics, "minic-ld: %s\n",
                shared_output ? "shared-pcrel-index-overflow"
                              : "relocation-index-overflow");
        return false;
    }
    pairs->slots = calloc(pairs->capacity, sizeof(*pairs->slots));
    if (pairs->slots == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:%s\n",
                shared_output ? "shared-pcrel-index" : "pcrel-index");
        pairs->capacity = 0U;
        return false;
    }
    return true;
}

static void minild_riscv_pairs_destroy(MiniLdRiscvPcrelPairs *pairs) {
    free(pairs->slots);
    pairs->slots = NULL;
    pairs->capacity = 0U;
}

/* All ELF input addends are signed, but work-section and symbol offsets are
 * unsigned.  Keep coordinates in unsigned space and reject out-of-bounds
 * anchors instead of wrapping negative addends into arbitrary HI20 sites. */
static bool minild_riscv_checked_add_u64_s64(uint64_t base,
                                             int64_t addend,
                                             uint64_t *result) {
    if (addend >= 0) {
        if (base > UINT64_MAX - (uint64_t)addend) {
            return false;
        }
        *result = base + (uint64_t)addend;
    } else {
        uint64_t magnitude = UINT64_C(0) - (uint64_t)addend;

        if (base < magnitude) {
            return false;
        }
        *result = base - magnitude;
    }
    return true;
}

/* Explicit, portable two's-complement interpretation of a 64-bit pattern.
 * In particular, the kernel half of RV64 address space is valid input:
 * neither an unsigned-to-signed out-of-range cast nor a signed subtraction
 * is needed to obtain its signed PC-relative displacement. */
static int64_t minild_riscv_signed_bits(uint64_t bits) {
    if (bits <= (uint64_t)INT64_MAX) {
        return (int64_t)bits;
    }
    return -INT64_C(1) - (int64_t)(UINT64_MAX - bits);
}

/* Preserve the low 64-bit S+A bit pattern without a signed overflow. */
static int64_t minild_riscv_addend_bits(uint64_t symbol_value,
                                       int64_t addend) {
    return minild_riscv_signed_bits(symbol_value + (uint64_t)addend);
}

/* RV64 PC-relative arithmetic is modulo 2^64.  A high-half place and target
 * may differ by a small positive displacement: rejecting place>INT64_MAX
 * would break legitimate kernel links.  Width-constrained relocations still
 * validate the resulting signed displacement at their individual encoder. */
static int64_t minild_riscv_pc_delta_bits(int64_t target, uint64_t place) {
    return minild_riscv_signed_bits((uint64_t)target - place);
}

/* The caller resolves target to S+A, except GOT/TLS GD where it supplies
 * the chosen slot/descriptor address (including TLS addend).  The target
 * dispatcher is therefore independent of PIE preemption and output-section
 * ownership.  CALL pairs are patched only when their output mode resolves
 * the final destination, which is later than PLT reservation for ET_DYN. */
static bool minild_riscv_apply_final_instruction(
    MiniLdRiscvPcrelPairs *pairs,
    MiniLdSection *source,
    const MiniLdReloc *reloc,
    int64_t target,
    uint64_t place,
    bool shared_output,
    const char *symbol_name,
    FILE *diagnostics) {
    int64_t delta = 0;

    /* Absolute HI20/LO12 never subtract P.  Only PC-relative forms need the
     * RV64 modular displacement; both low and high-half addresses work. */
    switch (reloc->type) {
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT:
    case R_RISCV_GOT_HI20:
    case R_RISCV_TLS_GD_HI20:
    case R_RISCV_PCREL_HI20:
    case R_RISCV_BRANCH:
    case R_RISCV_JAL:
    case R_RISCV_RVC_BRANCH:
    case R_RISCV_RVC_JUMP:
    case R_RISCV_32_PCREL:
        delta = minild_riscv_pc_delta_bits(target, place);
        break;
    default:
        break;
    }

    switch (reloc->type) {
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT:
        return static_patch_utype(source, reloc->offset,
                                  riscv_hi20(delta), diagnostics) &&
               static_patch_itype(source, reloc->offset + 4U,
                                  riscv_lo12(delta), diagnostics);
    case R_RISCV_GOT_HI20:
    case R_RISCV_TLS_GD_HI20:
    case R_RISCV_PCREL_HI20:
        /* ET_EXEC has no TLS GD descriptor reservation.  The type may be
         * valid for ET_DYN, but must remain rejected in static final links. */
        if (!shared_output && reloc->type == R_RISCV_TLS_GD_HI20) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-static-relocation:%u\n",
                    reloc->type);
            return false;
        }
        if (!static_patch_utype(source, reloc->offset,
                                riscv_hi20(delta), diagnostics)) {
            return false;
        }
        pcrel_insert(pairs->slots, pairs->capacity, reloc->section,
                     reloc->offset, delta);
        return true;
    case R_RISCV_HI20:
        return static_patch_utype(source, reloc->offset,
                                  riscv_hi20(target), diagnostics);
    case R_RISCV_LO12_I:
        return static_patch_itype(source, reloc->offset,
                                  riscv_lo12(target), diagnostics);
    case R_RISCV_LO12_S:
        return static_patch_stype(source, reloc->offset,
                                  riscv_lo12(target), diagnostics);
    case R_RISCV_BRANCH:
        return static_patch_branch(source, reloc->offset, delta, diagnostics);
    case R_RISCV_JAL:
        if (!shared_output &&
            ((delta & 1) != 0 || delta < -1048576 || delta > 1048574)) {
            fprintf(diagnostics,
                    "minic-ld: R_RISCV_JAL-overflow:"
                    "section=%s:offset=%llu:symbol=%s:"
                    "place=0x%llx:target=0x%llx:delta=%lld\n",
                    source->name,
                    (unsigned long long)reloc->offset,
                    symbol_name == NULL ? "<none>" : symbol_name,
                    (unsigned long long)place,
                    (unsigned long long)(uint64_t)target,
                    (long long)delta);
            return false;
        }
        return static_patch_jal(source, reloc->offset, delta, diagnostics);
    case R_RISCV_RVC_BRANCH:
        return static_patch_rvc_branch(source, reloc->offset,
                                       delta, diagnostics);
    case R_RISCV_RVC_JUMP:
        return static_patch_rvc_jump(source, reloc->offset,
                                     delta, diagnostics);
    case R_RISCV_32_PCREL:
        if (delta < INT32_MIN || delta > INT32_MAX ||
            source->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
            !range_ok((size_t)reloc->offset, 4U, source->size)) {
            if (shared_output) {
                fprintf(diagnostics,
                        "minic-ld: shared-R_RISCV_32_PCREL-range\n");
            } else {
                fprintf(diagnostics,
                        "minic-ld: R_RISCV_32_PCREL-overflow-or-range:delta=%lld\n",
                        (long long)delta);
            }
            return false;
        }
        store_u32le(source->data + (size_t)reloc->offset,
                    (uint32_t)(int32_t)delta);
        return true;
    default:
        fprintf(diagnostics, "minic-ld: %s:%u\n",
                shared_output ? "unsupported-shared-final-layout"
                              : "unsupported-static-relocation",
                reloc->type);
        return false;
    }
}

typedef enum MiniLdRiscvLo12Status {
    MINILD_RV_LO12_OK,
    MINILD_RV_LO12_MISSING,
    MINILD_RV_LO12_PATCH_FAILED
} MiniLdRiscvLo12Status;

static MiniLdRiscvLo12Status minild_riscv_apply_pcrel_lo12(
    const MiniLdRiscvPcrelPairs *pairs,
    MiniLdSection *source,
    const MiniLdReloc *reloc,
    size_t anchor_section,
    uint64_t anchor_offset,
    FILE *diagnostics) {
    int64_t delta;

    if (!pcrel_find(pairs->slots, pairs->capacity,
                    anchor_section, anchor_offset, &delta)) {
        return MINILD_RV_LO12_MISSING;
    }
    if ((reloc->type == R_RISCV_PCREL_LO12_I &&
         static_patch_itype(source, reloc->offset,
                            riscv_lo12(delta), diagnostics)) ||
        (reloc->type == R_RISCV_PCREL_LO12_S &&
         static_patch_stype(source, reloc->offset,
                            riscv_lo12(delta), diagnostics))) {
        return MINILD_RV_LO12_OK;
    }
    return MINILD_RV_LO12_PATCH_FAILED;
}

static bool minild_riscv_apply_arithmetic(MiniLdSection *section,
                                           const MiniLdReloc *reloc,
                                           int64_t target,
                                           bool shared_output,
                                           FILE *diagnostics) {
    const char *kind;
    size_t width;
    uint64_t operand = (uint64_t)target;
    size_t offset;

    switch (reloc->type) {
    case R_RISCV_ADD8:
    case R_RISCV_SUB8:
        kind = "ADD_SUB8";
        width = 1U;
        break;
    case R_RISCV_ADD16:
    case R_RISCV_SUB16:
        kind = "ADD_SUB16";
        width = 2U;
        break;
    case R_RISCV_ADD32:
    case R_RISCV_SUB32:
        kind = "ADD_SUB32";
        width = 4U;
        break;
    case R_RISCV_ADD64:
    case R_RISCV_SUB64:
        kind = "ADD_SUB64";
        width = 8U;
        break;
    case R_RISCV_SUB6:
        kind = "SUB6";
        width = 1U;
        break;
    case R_RISCV_SET6:
        kind = "SET6";
        width = 1U;
        break;
    case R_RISCV_SET8:
        kind = "SET8";
        width = 1U;
        break;
    case R_RISCV_SET16:
        kind = "SET16";
        width = 2U;
        break;
    case R_RISCV_SET32:
        kind = "SET32";
        width = 4U;
        break;
    default:
        fprintf(diagnostics,
                "minic-ld: internal-arithmetic-relocation:%u\n",
                reloc->type);
        return false;
    }
    if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
        !range_ok((size_t)reloc->offset, width, section->size)) {
        fprintf(diagnostics, "minic-ld: %s%s-offset-out-of-range\n",
                shared_output ? "shared-" : "R_RISCV_", kind);
        return false;
    }
    offset = (size_t)reloc->offset;

    switch (reloc->type) {
    case R_RISCV_ADD8:
        store_u8(section->data + offset,
                 (uint8_t)(load_u8(section->data + offset) + (uint8_t)operand));
        break;
    case R_RISCV_SUB8:
        store_u8(section->data + offset,
                 (uint8_t)(load_u8(section->data + offset) - (uint8_t)operand));
        break;
    case R_RISCV_ADD16:
        store_u16le(section->data + offset,
                    (uint16_t)(load_u16le(section->data + offset) +
                               (uint16_t)operand));
        break;
    case R_RISCV_SUB16:
        store_u16le(section->data + offset,
                    (uint16_t)(load_u16le(section->data + offset) -
                               (uint16_t)operand));
        break;
    case R_RISCV_ADD32:
        store_u32le(section->data + offset,
                    load_u32le(section->data + offset) + (uint32_t)operand);
        break;
    case R_RISCV_SUB32:
        store_u32le(section->data + offset,
                    load_u32le(section->data + offset) - (uint32_t)operand);
        break;
    case R_RISCV_ADD64:
        store_u64le(section->data + offset,
                    load_u64le(section->data + offset) + operand);
        break;
    case R_RISCV_SUB64:
        store_u64le(section->data + offset,
                    load_u64le(section->data + offset) - operand);
        break;
    case R_RISCV_SUB6: {
        uint8_t current = load_u8(section->data + offset);
        uint8_t result = (uint8_t)(((current & UINT8_C(0x3f)) -
                                   (operand & UINT64_C(0x3f))) &
                                  UINT8_C(0x3f));
        store_u8(section->data + offset,
                 (uint8_t)((current & UINT8_C(0xc0)) | result));
        break;
    }
    case R_RISCV_SET6: {
        uint8_t current = load_u8(section->data + offset);
        store_u8(section->data + offset,
                 (uint8_t)((current & UINT8_C(0xc0)) |
                           (operand & UINT64_C(0x3f))));
        break;
    }
    case R_RISCV_SET8:
        store_u8(section->data + offset, (uint8_t)operand);
        break;
    case R_RISCV_SET16:
        store_u16le(section->data + offset, (uint16_t)operand);
        break;
    case R_RISCV_SET32:
        store_u32le(section->data + offset, (uint32_t)operand);
        break;
    default:
        return false;
    }
    return true;
}

/* Eager RV64 PLT entry encoding belongs to the target.  ET_DYN decides
 * which symbol needs a PLT, the slot address, and its JUMP_SLOT relocation;
 * only this routine decides the machine instruction words.  Preserve the
 * four-word AUIPC/LD/JALR/NOP sequence and its original truncation rules. */
static uint32_t minild_riscv_encode_utype(unsigned opcode,
                                          unsigned rd,
                                          int64_t imm20) {
    return (((uint32_t)imm20 & UINT32_C(0xfffff)) << 12U) |
           ((uint32_t)rd << 7U) |
           (uint32_t)opcode;
}

static uint32_t minild_riscv_encode_itype(unsigned opcode,
                                          unsigned rd,
                                          unsigned funct3,
                                          unsigned rs1,
                                          int64_t imm12) {
    return (((uint32_t)imm12 & UINT32_C(0xfff)) << 20U) |
           ((uint32_t)rs1 << 15U) |
           ((uint32_t)funct3 << 12U) |
           ((uint32_t)rd << 7U) |
           (uint32_t)opcode;
}

static void minild_riscv_emit_plt_entry(unsigned char *data,
                                        size_t offset,
                                        uint64_t entry_address,
                                        uint64_t slot_address) {
    int64_t delta = minild_riscv_pc_delta_bits(
        minild_riscv_signed_bits(slot_address), entry_address);
    int64_t hi = riscv_hi20(delta);
    int64_t lo = riscv_lo12(delta);

    store_u32le(data + offset + 0U,
                minild_riscv_encode_utype(0x17U, 28U, hi));
    store_u32le(data + offset + 4U,
                minild_riscv_encode_itype(0x03U, 28U, 3U, 28U, lo));
    store_u32le(data + offset + 8U,
                minild_riscv_encode_itype(0x67U, 0U, 0U, 28U, 0));
    store_u32le(data + offset + 12U, UINT32_C(0x00000013));
}
