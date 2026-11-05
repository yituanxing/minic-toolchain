/* One immutable target policy for each output ELF ABI. The link invocation
 * borrows exactly one policy; Common never infers a target from the host or
 * from the first input file. Machine-specific admission remains here. */

#define MINILD_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)

struct MiniLdLayout;
struct MiniLdStaticTargetPlan;
struct MiniLdSharedTargetAbi;
static const struct MiniLdSharedTargetAbi *minild_riscv_shared_target_abi(void);
static bool minild_x86_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_x86_reserve_static_relocations(
    MiniLdState *state, struct MiniLdStaticTargetPlan *plan);
static bool minild_x86_fill_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static void minild_x86_destroy_static_relocations(
    struct MiniLdStaticTargetPlan *plan);
static bool minild_x86_synthesize_default_boundaries(
    MiniLdState *state, const struct MiniLdLayout *layout);
static bool minild_aarch64_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_aarch64_reserve_static_relocations(
    MiniLdState *state, struct MiniLdStaticTargetPlan *plan);
static bool minild_aarch64_fill_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static void minild_aarch64_destroy_static_relocations(
    struct MiniLdStaticTargetPlan *plan);
static bool minild_aarch64_synthesize_default_boundaries(
    MiniLdState *state, const struct MiniLdLayout *layout);
static bool minild_loongarch_apply_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static bool minild_loongarch_reserve_static_relocations(
    MiniLdState *state, struct MiniLdStaticTargetPlan *plan);
static bool minild_loongarch_fill_static_relocations(
    MiniLdState *state, const struct MiniLdLayout *layout,
    const struct MiniLdStaticTargetPlan *plan);
static void minild_loongarch_destroy_static_relocations(
    struct MiniLdStaticTargetPlan *plan);
static bool minild_loongarch_synthesize_default_boundaries(
    MiniLdState *state, const struct MiniLdLayout *layout);
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
static bool minild_riscv_replan_static_layout(
    MiniLdState *state, const struct MiniLdLayout *layout,
    struct MiniLdStaticTargetPlan *plan, bool *changed);
static bool minild_aarch64_replan_static_layout(
    MiniLdState *state, const struct MiniLdLayout *layout,
    struct MiniLdStaticTargetPlan *plan, bool *changed);
static bool minild_loongarch_relax_section(MiniLdState *state,
                                          size_t section_index,
                                          uint64_t address);
static bool minild_riscv_synthesize_gnu_default_boundaries(
    MiniLdState *state, const struct MiniLdLayout *layout);

typedef bool (*MiniLdTargetFlagsMerge)(MiniLdState *, uint32_t, bool,
                                       const char *);
typedef int (*MiniLdTargetSharedLink)(const char *, const MiniLdInput *,
                                      size_t, const MiniLdSharedOptions *,
                                      FILE *);

struct MiniLdTargetProfile {
    const char *emulation;
    unsigned char elf_class;
    unsigned char data_encoding;
    uint16_t machine;
    const char *script_arch;
    /* GNU linker scripts may use either the ISA or ABI-width arch spelling. */
    const char *script_arch_alternate;
    /* GNU OUTPUT_FORMAT belongs to the target ABI, never to generic
     * linker-script parsing or a host-dependent default. */
    const char *script_output_format;
    const char *pie_interpreter;
    uint32_t attributes_type;
    uint64_t static_image_base;
    size_t page_size;
    /* Segment ABI alignment is not always the section-layout page size.
     * GNU LoongArch -n still publishes a 64KB PT_LOAD alignment. */
    size_t nmagic_load_alignment;
    size_t static_note_alignment;
    bool supports_relocatable;
    bool supports_static;
    bool supports_static_script;
    bool supports_static_gc;
    bool supports_shared;
    bool supports_pie;
    /* Linux's fixed-address -shared -Bsymbolic -T kernel convention is
     * not interchangeable with a user-space DSO on other targets. */
    bool supports_kernel_shared_image;
    MiniLdTargetFlagsMerge merge_flags;
    /* SHF_MERGE may rewrite STT_SECTION+addend only when this relocation's
     * addend is a plain source byte offset (no PC bias/ISA split encoding).
     * Unknown relocations make their referenced source atom ineligible. */
    bool (*merge_section_addend_is_plain)(uint32_t type);
    bool (*relax_section)(MiniLdState *, size_t, uint64_t);
    /* Target recomputes size-changing machine-level decisions using the
     * complete placement, never a partially laid-out input fragment. */
    bool (*replan_static_layout)(MiniLdState *,
                                 const struct MiniLdLayout *,
                                 struct MiniLdStaticTargetPlan *, bool *);
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
    /* Full ET_DYN ABI is target-supplied, while an independent narrow
     * compatibility linker can expose only its admitted shared mode.
     * Core selects neither one by an architecture enumeration. */
    const struct MiniLdSharedTargetAbi *(*shared_abi)(void);
    MiniLdTargetSharedLink link_shared;
    bool (*valid_elf_flags)(uint32_t);
    /* Static CRTs differ in whether ELF headers belong to the first LOAD.
     * Scripted Linux images retain their explicit load-image contract. */
    bool map_static_elf_headers;
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

static bool minild_riscv_merge_addend_plain(uint32_t type) {
    return type == R_RISCV_32 || type == R_RISCV_64;
}

static bool minild_x86_merge_addend_plain(uint32_t type) {
    return type == R_X86_64_64 || type == R_X86_64_32 ||
           type == R_X86_64_32S;
}

static bool minild_aarch64_merge_addend_plain(uint32_t type) {
    return type == R_AARCH64_ABS64 || type == R_AARCH64_ABS32;
}

static bool minild_loongarch_merge_addend_plain(uint32_t type) {
    return type == R_LARCH_64 || type == R_LARCH_32;
}

/* GNU ld emulation spellings are the public target selection contract.
 * RV32's shared mode is its separate, narrow compat-vDSO implementation. */
static const MiniLdTargetProfile minild_target_profiles[] = {
    {"elf64lriscv", ELFCLASS64, ELFDATA2LSB, EM_RISCV, "riscv",
     NULL, "elf64-littleriscv", "/lib/ld-musl-riscv64.so.1", MINILD_SHT_RISCV_ATTRIBUTES,
     UINT64_C(0x10000), 4096U, 1U, 4U,
     true, true, true, true, true, true, false, minild_riscv_merge_flags,
     minild_riscv_merge_addend_plain,
     minild_riscv_relax_section,
     minild_riscv_replan_static_layout,
     minild_riscv_reserve_static_relocations,
     minild_riscv_fill_static_relocations,
     minild_riscv_apply_static_relocations,
     minild_riscv_destroy_static_relocations,
     minild_riscv_synthesize_gnu_default_boundaries,
     minild_riscv_shared_target_abi,
     minild_link_shared_elf64_riscv_inputs_options, NULL, true},
    {"elf32lriscv", ELFCLASS32, ELFDATA2LSB, EM_RISCV, "riscv",
     NULL, "elf32-littleriscv", NULL, MINILD_SHT_RISCV_ATTRIBUTES,
     UINT64_C(0x10000), 4096U, 1U, 4U,
     true, true, true, true, true, false, false, minild_riscv_merge_flags,
     minild_riscv_merge_addend_plain,
     minild_riscv_relax_section,
     minild_riscv_replan_static_layout,
     minild_riscv_reserve_static_relocations,
     minild_riscv_fill_static_relocations,
     minild_riscv_apply_static_relocations,
     minild_riscv_destroy_static_relocations,
     minild_riscv_synthesize_gnu_default_boundaries,
     NULL, minild_link_elf32_compat_vdso, NULL, true},
    {"elf_x86_64", ELFCLASS64, ELFDATA2LSB, EM_X86_64, "i386:x86-64",
     NULL, "elf64-x86-64", NULL, 0U, UINT64_C(0x400000), 4096U, 1U, 4U,
     true, true, true, true, false, false, false,
     minild_exact_merge_flags, minild_x86_merge_addend_plain, NULL,
     NULL,
     minild_x86_reserve_static_relocations,
     minild_x86_fill_static_relocations,
     minild_x86_apply_static_relocations,
     minild_x86_destroy_static_relocations,
     minild_x86_synthesize_default_boundaries,
     NULL, NULL, minild_zero_elf_flags, true},
    {"aarch64linux", ELFCLASS64, ELFDATA2LSB, EM_AARCH64, "aarch64",
     NULL, "elf64-littleaarch64", NULL, 0U, UINT64_C(0x400000), 4096U, 1U, 4U,
     true, true, true, true, false, false, true,
     minild_exact_merge_flags, minild_aarch64_merge_addend_plain, NULL,
     minild_aarch64_replan_static_layout,
     minild_aarch64_reserve_static_relocations,
     minild_aarch64_fill_static_relocations,
     minild_aarch64_apply_static_relocations,
     minild_aarch64_destroy_static_relocations,
     minild_aarch64_synthesize_default_boundaries,
     NULL, NULL, minild_zero_elf_flags, true},
    {"elf64loongarch", ELFCLASS64, ELFDATA2LSB, EM_LOONGARCH, "loongarch64",
     "loongarch", "elf64-loongarch", NULL, 0U, UINT64_C(0x120000000), 4096U,
     65536U, 8U,
     true, true, true, true, false, false, false,
     minild_exact_merge_flags, minild_loongarch_merge_addend_plain,
     minild_loongarch_relax_section,
     NULL,
     minild_loongarch_reserve_static_relocations,
     minild_loongarch_fill_static_relocations,
     minild_loongarch_apply_static_relocations,
     minild_loongarch_destroy_static_relocations,
     minild_loongarch_synthesize_default_boundaries,
     NULL, NULL, minild_loongarch_v1_elf_flags, true}
};

/* Page size is one invocation-level ELF layout contract. Every static
 * placement and PHDR computation must use the same value when GNU
 * -z max-page-size overrides the Target's default. */
static size_t minild_static_page_size(const MiniLdState *state) {
    return state->max_page_size != 0U
               ? (size_t)state->max_page_size
               : state->target->page_size;
}

static const MiniLdTargetProfile *minild_target_find(const char *emulation) {
    size_t i;

    if (emulation == NULL) {
        return NULL;
    }
    /* GNU BFD's AArch64 bare emulation and our Linux spelling describe
     * the same ELF64 LE machine/ABI. Keep one target descriptor. */
    if (strcmp(emulation, "aarch64elf") == 0) {
        emulation = "aarch64linux";
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

bool minild_emulation_supports_kernel_shared_image(const char *emulation) {
    const MiniLdTargetProfile *target = minild_target_find(emulation);

    return target != NULL && target->supports_kernel_shared_image;
}

/* AArch64's Linux kernel -shared -Bsymbolic -T link is not a user-space
 * DSO.  Target-owned R_AARCH64_RELATIVE records in .rela.dyn preserve
 * allocated absolute pointers without pre-applying their image-base values.
 * Other relocations admitted here can be materialized statically because
 * they are unchanged by a common 4KiB-aligned rebase of S and P.
 *
 *   PC-relative: (S + delta) - (P + delta) = S - P, including ADR/LD/branch
 *                immediates and full-width PREL data fields.
 *   Page-relative: PAGE(S + delta) - PAGE(P + delta) = PAGE(S) - PAGE(P).
 *   Low 12:      (S + delta) & 0xfff = S & 0xfff.
 *
 * MOVW, GOT/TLS and static resolver relocations require their own runtime
 * handling and must not silently be accepted as ordinary static fixups.
 * This policy applies only to the specifically admitted kernel ET_DYN mode. */
#ifndef R_AARCH64_GOTPCREL32
#define R_AARCH64_GOTPCREL32 315
#endif
static bool minild_aarch64_kernel_rebase_invariant(uint32_t type) {
    switch (type) {
    /* No value, or a full-width PC-relative scalar. */
    case R_AARCH64_NONE:
    /* Converted to R_AARCH64_RELATIVE where the target is image-relative. */
    case R_AARCH64_ABS64:
    /* Post-layout checked: only image-invariant absolute values are legal. */
    case R_AARCH64_ABS32:
    case R_AARCH64_PREL32:
    case R_AARCH64_PREL64:
    /* PC-relative and page-relative A64 instruction fields. */
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_TSTBR14:
    case R_AARCH64_CONDBR19:
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    /* Only the low 12 bits of page-aligned rebasing participate. */
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
    /* GOT address calculations are invariant under common rebasing; the
     * actual GOT pointer is separately emitted as R_AARCH64_RELATIVE. */
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_GOTPCREL32:
        return true;
    default:
        return false;
    }
}

static bool minild_target_validate_kernel_shared_relocations(
    const MiniLdState *state) {
    size_t i;

    if (state->target == NULL ||
        !state->target->supports_kernel_shared_image) {
        return false;
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        uint32_t type = reloc->type;

        if (reloc->section >= state->work.count) {
            return false;
        }
        if ((state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (state->work.sections[reloc->section].gc_discarded ||
            state->work.sections[reloc->section].script_discard) {
            continue;
        }
        if (!minild_aarch64_kernel_rebase_invariant(type)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-kernel-shared-runtime-relocation:%u"
                    ":source=%s:target=%s:offset=0x%llx\n",
                    (unsigned)type,
                    state->work.sections[reloc->section].name,
                    reloc->symbol < state->resolver.count
                        ? state->resolver.entries[reloc->symbol].name : "<none>",
                    (unsigned long long)reloc->offset);
            return false;
        }
    }
    return true;
}

bool minild_emulation_supports_endianness(const char *emulation,
                                          bool little_endian) {
    const MiniLdTargetProfile *target = minild_target_find(emulation);

    return target != NULL &&
           target->data_encoding == (little_endian ? ELFDATA2LSB : ELFDATA2MSB);
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
           (strcmp(arch, target->script_arch) == 0 ||
            (target->script_arch_alternate != NULL &&
             strcmp(arch, target->script_arch_alternate) == 0));
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
