/* RV64 ELF target relocation encoding and HI20/LO12 pairing primitives.
 * Included once by linker_layout.inc after the byte load/store accessors;
 * static and shared final-link consumers use the same checked encoders.
 */

static int64_t riscv_hi20(int64_t value) {
    return (value + INT64_C(0x800)) >> 12;
}

static int64_t riscv_lo12(int64_t value) {
    int64_t high = riscv_hi20(value);
    return value - (high << 12);
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
