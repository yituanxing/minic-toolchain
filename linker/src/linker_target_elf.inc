/* One immutable target policy for each output ELF ABI. The link invocation
 * borrows exactly one policy; Common never infers a target from the host or
 * from the first input file. Machine-specific admission remains here. */

#define MINILD_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)

struct MiniLdLayout;
struct MiniLdStaticTargetPlan;
static bool minild_x86_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_aarch64_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_loongarch_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_riscv_reserve_static_relocations(
    MiniLdState *state, struct MiniLdStaticTargetPlan *plan);
static bool minild_riscv_fill_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_riscv_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static void minild_riscv_destroy_static_relocations(
    struct MiniLdStaticTargetPlan *plan);
static bool minild_riscv_relax_section(MiniLdState *state,
                                      size_t section_index,
                                      uint64_t address);
static bool minild_riscv_synthesize_gnu_default_boundaries(
    MiniLdState *state, const struct MiniLdLayout *layout);

typedef bool (*MiniLdTargetFlagsMerge)(MiniLdState *, uint32_t, bool,
                                       const char *);
typedef enum MiniLdTargetSharedBackend {
    MINILD_SHARED_UNSUPPORTED,
    MINILD_SHARED_RISCV_ELF64,
    MINILD_SHARED_RISCV32_COMPAT_VDSO
} MiniLdTargetSharedBackend;

struct MiniLdTargetProfile {
    const char *emulation;
    unsigned char elf_class;
    unsigned char data_encoding;
    uint16_t machine;
    const char *script_arch;
    const char *pie_interpreter;
    uint32_t attributes_type;
    uint64_t static_image_base;
    size_t page_size;
    bool supports_relocatable;
    bool supports_static;
    bool supports_static_script;
    bool supports_static_gc;
    bool supports_shared;
    bool supports_pie;
    MiniLdTargetFlagsMerge merge_flags;
    bool (*relax_section)(MiniLdState *, size_t, uint64_t);
    /* The target reserves size-changing synthetic relocation sections before
     * placement, then fills and patches them only after final addresses.
     * The plan is opaque to Core; NULL hooks are valid for no-op stages. */
    bool (*reserve_static_relocations)(MiniLdState *,
                                        struct MiniLdStaticTargetPlan *);
    bool (*fill_static_relocations)(MiniLdState *,
                                     const struct MiniLdLayout *,
                                     const struct MiniLdStaticTargetPlan *);
    bool (*apply_static_relocations)(MiniLdState *,
                                      const struct MiniLdLayout *,
                                      const struct MiniLdStaticTargetPlan *);
    void (*destroy_static_relocations)(struct MiniLdStaticTargetPlan *);
    /* GNU linker-script PROVIDE symbols with psABI-specific names belong
     * to Target. Common owns the language-independent init/fini arrays. */
    bool (*synthesize_default_symbols)(MiniLdState *,
                                        const struct MiniLdLayout *);
    MiniLdTargetSharedBackend shared_backend;
    bool (*valid_elf_flags)(uint32_t);
};

static bool minild_riscv_merge_flags(MiniLdState *state,
                                     uint32_t incoming_flags,
                                     bool incoming_has_code,
                                     const char *path);
static bool minild_exact_merge_flags(MiniLdState *state,
                                     uint32_t incoming_flags,
                                     bool incoming_has_code,
                                     const char *path);
static bool minild_zero_elf_flags(uint32_t flags);
static bool minild_loongarch_v1_elf_flags(uint32_t flags);

/* GNU ld emulation spellings are the public target selection contract.
 * RV32's shared mode is its separate, narrow compat-vDSO implementation. */
static const MiniLdTargetProfile minild_target_profiles[] = {
    {"elf64lriscv", ELFCLASS64, ELFDATA2LSB, EM_RISCV, "riscv",
     "/lib/ld-musl-riscv64.so.1", MINILD_SHT_RISCV_ATTRIBUTES,
     UINT64_C(0x10000), 4096U,
     true, true, true, true, true, true, minild_riscv_merge_flags,
     minild_riscv_relax_section,
     minild_riscv_reserve_static_relocations,
     minild_riscv_fill_static_relocations,
     minild_riscv_apply_static_relocations,
     minild_riscv_destroy_static_relocations,
     minild_riscv_synthesize_gnu_default_boundaries,
     MINILD_SHARED_RISCV_ELF64, NULL},
    {"elf32lriscv", ELFCLASS32, ELFDATA2LSB, EM_RISCV, "riscv",
     NULL, MINILD_SHT_RISCV_ATTRIBUTES,
     UINT64_C(0x10000), 4096U,
     true, false, false, false, true, false, minild_riscv_merge_flags,
     minild_riscv_relax_section,
     minild_riscv_reserve_static_relocations,
     minild_riscv_fill_static_relocations,
     minild_riscv_apply_static_relocations,
     minild_riscv_destroy_static_relocations, NULL,
     MINILD_SHARED_RISCV32_COMPAT_VDSO, NULL},
    {"elf_x86_64", ELFCLASS64, ELFDATA2LSB, EM_X86_64, "i386:x86-64",
     NULL, 0U, UINT64_C(0x400000), 4096U,
     true, true, false, false, false, false,
     minild_exact_merge_flags, NULL, NULL, NULL,
     minild_x86_apply_static_relocations, NULL, NULL,
     MINILD_SHARED_UNSUPPORTED, minild_zero_elf_flags},
    {"aarch64linux", ELFCLASS64, ELFDATA2LSB, EM_AARCH64, "aarch64",
     NULL, 0U, UINT64_C(0x400000), 4096U,
     true, true, false, false, false, false,
     minild_exact_merge_flags, NULL, NULL, NULL,
     minild_aarch64_apply_static_relocations, NULL, NULL,
     MINILD_SHARED_UNSUPPORTED, minild_zero_elf_flags},
    {"elf64loongarch", ELFCLASS64, ELFDATA2LSB, EM_LOONGARCH, "loongarch64",
     NULL, 0U, UINT64_C(0x400000), 4096U,
     true, true, false, false, false, false,
     minild_exact_merge_flags, NULL, NULL, NULL,
     minild_loongarch_apply_static_relocations, NULL, NULL,
     MINILD_SHARED_UNSUPPORTED, minild_loongarch_v1_elf_flags}
};

static const MiniLdTargetProfile *minild_target_find(const char *emulation) {
    size_t i;

    if (emulation == NULL) {
        return NULL;
    }
    for (i = 0U;
         i < sizeof(minild_target_profiles) / sizeof(minild_target_profiles[0]);
         ++i) {
        if (strcmp(minild_target_profiles[i].emulation, emulation) == 0) {
            return &minild_target_profiles[i];
        }
    }
    return NULL;
}

bool minild_emulation_supports_mode(const char *emulation,
                                    MiniLdLinkMode mode) {
    const MiniLdTargetProfile *target = minild_target_find(emulation);

    if (target == NULL) {
        return false;
    }
    switch (mode) {
    case MINILD_LINK_RELOCATABLE: return target->supports_relocatable;
    case MINILD_LINK_STATIC: return target->supports_static;
    case MINILD_LINK_SHARED: return target->supports_shared;
    case MINILD_LINK_PIE: return target->supports_pie;
    }
    return false;
}

static bool minild_target_accept_elf(const MiniLdState *state,
                                     const MiniElfView *elf,
                                     uint16_t type) {
    return state->target != NULL &&
           elf->elf_class == state->target->elf_class &&
           elf->data_encoding == state->target->data_encoding &&
           elf->machine == state->target->machine &&
           elf->type == type;
}

static void minild_target_set_relocatable_identity(
    const MiniLdTargetProfile *target, MiniElfRelocatableSpec *spec) {
    spec->elf_class = target->elf_class;
    spec->data_encoding = target->data_encoding;
    spec->machine = target->machine;
}

/* Linker-script OUTPUT_ARCH and the default PIE loader are target ABI
 * properties. Generic ET_EXEC/ET_DYN drivers only validate or borrow them;
 * they must not enumerate machine names or hardcode a target runtime. */
static bool minild_target_accept_script_arch(
    const MiniLdTargetProfile *target, const char *arch) {
    return target != NULL && arch != NULL &&
           strcmp(arch, target->script_arch) == 0;
}

static const char *minild_target_default_pie_interpreter(
    const MiniLdTargetProfile *target) {
    return target == NULL ? NULL : target->pie_interpreter;
}

static bool minild_target_is_attributes_section(
    const MiniLdState *state, uint32_t section_type) {
    return state->target != NULL && state->target->attributes_type != 0U &&
           section_type == state->target->attributes_type;
}

/* An attributes section has one 'A' marker followed by length-delimited
 * vendor records. Preserve even unknown vendors, checking every length. */
static bool minild_riscv_attributes_valid(const unsigned char *bytes,
                                          size_t size) {
    size_t pos = 1U;

    if (size < 7U || bytes[0] != (unsigned char)'A') {
        return false;
    }
    while (pos < size) {
        size_t vendor_end;
        uint32_t length;
        if (!range_ok(pos, 4U, size)) {
            return false;
        }
        length = (uint32_t)bytes[pos] |
                 ((uint32_t)bytes[pos + 1U] << 8U) |
                 ((uint32_t)bytes[pos + 2U] << 16U) |
                 ((uint32_t)bytes[pos + 3U] << 24U);
        if (length < 6U || !range_ok(pos, (size_t)length, size)) {
            return false;
        }
        vendor_end = pos + 4U;
        while (vendor_end < pos + (size_t)length &&
               bytes[vendor_end] != 0U) {
            ++vendor_end;
        }
        if (vendor_end == pos + (size_t)length ||
            vendor_end == pos + 4U) {
            return false;
        }
        pos += (size_t)length;
    }
    return pos == size;
}

static bool minild_target_accept_attributes(
    const MiniLdState *state, const MiniElfView *elf,
    size_t section_index, const char *name,
    const MiniElfSection *section, const unsigned char **data_out,
    size_t *size_out) {
    return minild_target_is_attributes_section(state, section->type) &&
           strcmp(name, ".riscv.attributes") == 0 &&
           section->flags == 0U &&
           minielf_section_data(elf, section_index, data_out, size_out) &&
           minild_riscv_attributes_valid(*data_out, *size_out);
}

/* Project one leading 'A' and all following vendor subsections, exactly as
 * source admission validated. The destination is ordinary work-section data. */
static bool minild_target_append_attributes(MiniLdSection *output,
                                            const MiniLdInputSection *input) {
    return input->size != 0U && input->data != NULL &&
           section_append_data(output,
                               input->data +
                                   (output->size == 0U ? 0U : 1U),
                               input->size -
                                   (output->size == 0U ? 0U : 1U));
}

static bool minild_riscv_merge_flags(MiniLdState *state,
                                          uint32_t incoming_flags,
                                          bool incoming_has_code,
                                          const char *path) {
    uint32_t merged_flags = 0U;
    const char *abi_reason = NULL;

    if (!minild_riscv_elf_abi_merge(
            state->elf_flags, incoming_flags,
            state->processed_object_count != 0U, incoming_has_code,
            &merged_flags, &abi_reason)) {
        fprintf(state->diagnostics,
                "minic-ld: incompatible-riscv-elf-flags:%s:%s:"
                "current=0x%08x:incoming=0x%08x\n",
                path,
                abi_reason == NULL ? "unknown" : abi_reason,
                (unsigned)state->elf_flags, (unsigned)incoming_flags);
        return false;
    }
    state->elf_flags = merged_flags;
    return true;
}

/* Non-RISC-V psABIs cannot silently combine differing ELF flag contracts.
 * x86-64/AArch64 normally use zero; LoongArch64 carries LP64 ABI flags. */
static bool minild_zero_elf_flags(uint32_t flags) {
    return flags == 0U;
}

/* LoongArch psABI v1 has ELF flag version 0x40 and LP64S/F/D ABI codes
 * 1, 2, 3 respectively. Other version/ABI combinations are not accepted
 * by this target profile even if all source objects agree with each other. */
static bool minild_loongarch_v1_elf_flags(uint32_t flags) {
    return flags == UINT32_C(0x41) || flags == UINT32_C(0x42) ||
           flags == UINT32_C(0x43);
}

static bool minild_exact_merge_flags(MiniLdState *state,
                                     uint32_t incoming_flags,
                                     bool incoming_has_code,
                                     const char *path) {
    (void)incoming_has_code;
    if (state->processed_object_count != 0U &&
        state->elf_flags != incoming_flags) {
        fprintf(state->diagnostics,
                "minic-ld: incompatible-target-elf-flags:%s:"
                "current=0x%08x:incoming=0x%08x\n",
                path, (unsigned)state->elf_flags,
                (unsigned)incoming_flags);
        return false;
    }
    if (state->target->valid_elf_flags == NULL ||
        !state->target->valid_elf_flags(incoming_flags)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-target-elf-flags:%s:0x%08x\n",
                path, (unsigned)incoming_flags);
        return false;
    }
    state->elf_flags = incoming_flags;
    return true;
}

static bool minild_target_merge_elf_flags(MiniLdState *state,
                                          uint32_t incoming_flags,
                                          bool incoming_has_code,
                                          const char *path) {
    return state->target != NULL &&
           state->target->merge_flags(state, incoming_flags,
                                      incoming_has_code, path);
}
