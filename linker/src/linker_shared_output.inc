typedef struct MiniLdSharedImage {
    size_t dynstr_section;
    size_t dynsym_section;
    size_t hash_section;
    size_t gnu_hash_section;
    size_t versym_section;
    size_t verdef_section;
    uint32_t version_name_offset;
    const MiniLdScript *version_script;
    const MiniLdScript *layout_script;
    size_t eh_frame_hdr_section;
    size_t eh_frame_fde_count;
    size_t rela_section;
    size_t got_section;
    size_t plt_section;
    size_t gotplt_section;
    size_t rela_plt_section;
    size_t dynamic_section;
    size_t interp_section;
    uint32_t soname_offset;
    uint32_t *needed_offsets;
    size_t needed_count;
    size_t *dynsym_index;
    uint32_t *dynstr_name_offset;
    size_t *plt_index;
    size_t *got_slot_offset;
    size_t *tls_gd_offset;
    bool *preemptible;
    unsigned char *dynamic_list_data;
    size_t dynamic_list_size;
    size_t dynsym_count;
    size_t symbol_map_capacity;
    size_t prepared_symbol_count;
    size_t rela_count;
    size_t plt_count;
    size_t got_count;
    size_t tls_gd_count;
    bool have_soname;
    bool have_gnu_hash;
    bool have_eh_frame_hdr;
    bool have_interp;
    bool have_textrel;
    bool pie;
    bool have_dynamic_list;
} MiniLdSharedImage;

static void shared_image_destroy(MiniLdSharedImage *shared) {
    free(shared->dynsym_index);
    free(shared->dynstr_name_offset);
    free(shared->plt_index);
    free(shared->got_slot_offset);
    free(shared->tls_gd_offset);
    free(shared->preemptible);
    free(shared->needed_offsets);
    free(shared->dynamic_list_data);
    shared->dynsym_index = NULL;
    shared->dynstr_name_offset = NULL;
    shared->plt_index = NULL;
    shared->got_slot_offset = NULL;
    shared->tls_gd_offset = NULL;
    shared->preemptible = NULL;
    shared->needed_offsets = NULL;
    shared->needed_count = 0U;
    shared->dynamic_list_data = NULL;
    shared->dynamic_list_size = 0U;
    shared->dynsym_count = 0U;
    shared->rela_count = 0U;
    shared->plt_count = 0U;
    shared->got_count = 0U;
    shared->tls_gd_count = 0U;
    shared->have_soname = false;
    shared->have_gnu_hash = false;
    shared->have_interp = false;
    shared->have_textrel = false;
    shared->pie = false;
    shared->have_dynamic_list = false;
}

static uint32_t shared_elf_hash(const char *name) {
    uint32_t hash = 0U;

    while (*name != '\0') {
        uint32_t high;

        hash = (hash << 4U) + (unsigned char)*name++;
        high = hash & UINT32_C(0xf0000000);
        if (high != 0U) {
            hash ^= high >> 24U;
        }
        hash &= ~high;
    }
    return hash;
}

static uint32_t shared_gnu_symbol_hash(const char *name) {
    uint32_t hash = UINT32_C(5381);
    while (*name != '\0') {
        hash = hash * UINT32_C(33) + (unsigned char)*name++;
    }
    return hash;
}

static bool shared_version_exports(const MiniLdScript *script,
                                   const char *name) {
    size_t i;
    if (script == NULL) {
        return true;
    }
    for (i = 0U; i < script->version_global_count; ++i) {
        if (strcmp(script->version_globals[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool shared_symbol_eligible(const MiniLdState *state, size_t index,
                                   const MiniLdScript *version) {
    const MiniLdResolvedSymbol *symbol = &state->resolver.entries[index];
    unsigned bind = ELF64_ST_BIND(symbol->info);
    unsigned visibility = ELF64_ST_VISIBILITY(symbol->other);

    /* Script assignments give their canonical UND slots a final definition.
     * An existing hidden/protected reference still owns its visibility:
     * a hidden scripted symbol must never become a dynamic export. */
    if (symbol->script_planned_defined) {
        return (bind == STB_GLOBAL || bind == STB_WEAK) &&
               symbol->name[0] != '\0' &&
               (visibility == STV_DEFAULT ||
                visibility == STV_PROTECTED) &&
               (version == NULL ||
                shared_version_exports(version, symbol->name));
    }
    if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
        symbol->name[0] == '\0' ||
        (visibility != STV_DEFAULT && visibility != STV_PROTECTED)) {
        return false;
    }
    if (version != NULL && symbol->section != MINILD_SECTION_UNDEF &&
        !shared_version_exports(version, symbol->name)) {
        return false;
    }
    if (symbol->section >= 0) {
        size_t section = (size_t)symbol->section;
        if (section >= state->work.count ||
            (state->work.sections[section].flags & SHF_ALLOC) == 0U) {
            return false;
        }
    }
    return true;
}

static bool shared_dynamic_list_name_char(unsigned char ch) {
    return (ch >= (unsigned char)'a' && ch <= (unsigned char)'z') ||
           (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
           (ch >= (unsigned char)'0' && ch <= (unsigned char)'9') ||
           ch == (unsigned char)'_' ||
           ch == (unsigned char)'.' ||
           ch == (unsigned char)'$' ||
           ch == (unsigned char)'@';
}

static bool shared_dynamic_list_contains(const MiniLdSharedImage *shared,
                                         const char *name) {
    size_t name_size = strlen(name);
    size_t i = 0U;

    if (!shared->have_dynamic_list) {
        return true;
    }
    while (i < shared->dynamic_list_size) {
        size_t start;
        size_t size;

        while (i < shared->dynamic_list_size &&
               !shared_dynamic_list_name_char(shared->dynamic_list_data[i])) {
            ++i;
        }
        start = i;
        while (i < shared->dynamic_list_size &&
               shared_dynamic_list_name_char(shared->dynamic_list_data[i])) {
            ++i;
        }
        size = i - start;
        if (size == name_size &&
            size != 0U &&
            memcmp(shared->dynamic_list_data + start, name, size) == 0) {
            return true;
        }
    }
    return false;
}

static bool shared_load_dynamic_list(MiniLdState *state,
                                     MiniLdSharedImage *shared,
                                     const char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        fprintf(state->diagnostics,
                "minic-ld: empty-dynamic-list\n");
        return false;
    }
    if (!read_file(path,
                   &shared->dynamic_list_data,
                   &shared->dynamic_list_size,
                   state->diagnostics)) {
        return false;
    }
    shared->have_dynamic_list = true;
    return true;
}

static bool shared_symbol_preemptible(const MiniLdState *state,
                                      const MiniLdSharedImage *shared,
                                      size_t index) {
    const MiniLdResolvedSymbol *symbol;
    unsigned bind;
    unsigned visibility;

    if (index >= state->resolver.count) {
        return false;
    }
    symbol = &state->resolver.entries[index];
    if (symbol->script_planned_defined) {
        bind = ELF64_ST_BIND(symbol->info);
        return (bind == STB_GLOBAL || bind == STB_WEAK) &&
               !shared->pie &&
               ELF64_ST_VISIBILITY(symbol->other) == STV_DEFAULT &&
               shared_dynamic_list_contains(shared, symbol->name);
    }
    if (symbol->section == MINILD_SECTION_UNDEF) {
        return true;
    }
    /* An ET_DYN executable's own definitions bind locally.  Unlike a DSO,
     * a PIE must not route calls to its defined globals through JUMP_SLOT. */
    if (shared->pie) {
        return false;
    }
    bind = ELF64_ST_BIND(symbol->info);
    visibility = ELF64_ST_VISIBILITY(symbol->other);
    if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
        visibility != STV_DEFAULT) {
        return false;
    }
    return shared_dynamic_list_contains(shared, symbol->name);
}

static bool shared_symbol_needs_plt(const MiniLdState *state,
                                    const MiniLdSharedImage *shared,
                                    size_t index) {
    if (index >= state->resolver.count) {
        return false;
    }
    if (state->resolver.entries[index].section == MINILD_SECTION_UNDEF &&
        !state->resolver.entries[index].script_planned_defined) {
        return true;
    }
    return shared->preemptible[index];
}

/* A nonpreemptible SHN_ABS address is a load-independent constant.  Script
 * declarations can establish this fact before their values are evaluated,
 * so neither a dynamic RELATIVE relocation nor a loader textrel is needed. */
static bool shared_symbol_is_static_absolute(const MiniLdState *state,
                                             const MiniLdSharedImage *shared,
                                             size_t index) {
    const MiniLdResolvedSymbol *symbol;

    if (index >= state->resolver.count || shared->preemptible[index]) {
        return false;
    }
    symbol = &state->resolver.entries[index];
    /* Layout may later turn a section-relative script assignment into ABS.
     * Such a change cannot retroactively shrink .rela.dyn: only guaranteed
     * pre-layout ABS definitions qualify for the static-relocation budget. */
    if (symbol->script_planned_defined) {
        return symbol->script_planned_absolute;
    }
    return symbol->section == MINILD_SECTION_ABS;
}



static bool shared_relocation_is_final_layout(uint32_t type) {
    return minild_riscv_reloc_final_layout(type);
}

#include "linker_shared_reloc_scan.inc"

static bool shared_define_linker_symbols_pre_layout(
    MiniLdState *state,
    size_t dynamic_section) {
    size_t index;
    MiniLdResolvedSymbol *symbol;

    index = symbol_resolver_lookup(&state->resolver, "_DYNAMIC");
    if (index != SIZE_MAX) {
        if (dynamic_section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-dynamic-section\n");
            return false;
        }
        symbol = &state->resolver.entries[index];
        if (symbol->section != MINILD_SECTION_UNDEF &&
            symbol->section != (int)dynamic_section) {
            fprintf(state->diagnostics,
                    "minic-ld: conflicting-linker-symbol:_DYNAMIC\n");
            return false;
        }
        symbol->section = (int)dynamic_section;
        symbol->value = 0U;
        symbol->size = 0U;
        symbol->info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
        symbol->other = STV_HIDDEN;
    }

    index = symbol_resolver_lookup(&state->resolver, "__ehdr_start");
    if (index != SIZE_MAX) {
        symbol = &state->resolver.entries[index];
        if (symbol->section != MINILD_SECTION_UNDEF &&
            symbol->section != MINILD_SECTION_ABS) {
            fprintf(state->diagnostics,
                    "minic-ld: conflicting-linker-symbol:__ehdr_start\n");
            return false;
        }
        symbol->section = MINILD_SECTION_ABS;
        symbol->value = 0U;
        symbol->size = 0U;
        symbol->info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        symbol->other = STV_HIDDEN;
    }
    return true;
}

static bool shared_finalize_ehdr_symbol(MiniLdState *state,
                                        const MiniLdLayout *layout) {
    size_t index = symbol_resolver_lookup(&state->resolver, "__ehdr_start");
    uint64_t header_vaddr;

    if (index == SIZE_MAX) {
        return true;
    }
    if (!layout->have_rx ||
        (uint64_t)layout->rx_file_offset > layout->rx_vaddr) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-header-load-layout\n");
        return false;
    }
    header_vaddr =
        layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
    state->resolver.entries[index].section = MINILD_SECTION_ABS;
    state->resolver.entries[index].value = header_vaddr;
    return true;
}

#include "linker_eh_frame.inc"

static bool shared_prepare_metadata(MiniLdState *state,
                                    const char *soname,
                                    const char *dynamic_list_path,
                                    const char *needed_name,
                                    const char *interpreter_path,
                                    bool pie,
                                    bool hash_style_both,
                                    bool eh_frame_hdr,
                                    const MiniLdScript *version_script,
                                    MiniLdSharedImage *shared) {
    MiniLdSection *dynstr;
    MiniLdSection *dynsym;
    MiniLdSection *hash;
    MiniLdSection *rela;
    MiniLdSection *got;
    MiniLdSection *plt;
    MiniLdSection *gotplt;
    MiniLdSection *rela_plt;
    MiniLdSection *dynamic;
    size_t dynsym_size;
    size_t hash_words;
    size_t dynamic_entries;
    size_t i;

    memset(shared, 0, sizeof(*shared));
    shared->dynstr_section = SIZE_MAX;
    shared->dynsym_section = SIZE_MAX;
    shared->hash_section = SIZE_MAX;
    shared->gnu_hash_section = SIZE_MAX;
    shared->versym_section = SIZE_MAX;
    shared->verdef_section = SIZE_MAX;
    shared->version_script = version_script;
    shared->eh_frame_hdr_section = SIZE_MAX;
    shared->have_gnu_hash = hash_style_both;
    shared->rela_section = SIZE_MAX;
    shared->got_section = SIZE_MAX;
    shared->plt_section = SIZE_MAX;
    shared->gotplt_section = SIZE_MAX;
    shared->rela_plt_section = SIZE_MAX;
    shared->dynamic_section = SIZE_MAX;
    shared->interp_section = SIZE_MAX;
    shared->pie = pie;
    /* Script-defined names are registered as canonical unresolved slots
     * before this phase.  No resolver index may be appended during layout:
     * dynamic table size and hash indices are fixed here, not afterward. */
    shared->prepared_symbol_count = state->resolver.count;
    shared->symbol_map_capacity = shared->prepared_symbol_count;
    if (shared->symbol_map_capacity >
        SIZE_MAX / sizeof(*shared->dynsym_index)) {
        fprintf(state->diagnostics,
                "minic-ld: script-symbol-map-overflow\n");
        goto fail;
    }
    if (shared->symbol_map_capacity == 0U) {
        shared->symbol_map_capacity = 1U;
    }

    shared->dynsym_index =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->dynsym_index));
    shared->dynstr_name_offset =
        calloc(shared->symbol_map_capacity,
               sizeof(*shared->dynstr_name_offset));
    shared->plt_index =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->plt_index));
    shared->got_slot_offset =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->got_slot_offset));
    shared->tls_gd_offset =
        malloc(shared->symbol_map_capacity *
               sizeof(*shared->tls_gd_offset));
    shared->preemptible =
        calloc(shared->symbol_map_capacity,
               sizeof(*shared->preemptible));
    {
        size_t maximum_needed = input_graph_dso_needed_count(&state->inputs) +
            (needed_name != NULL && needed_name[0] != '\0' ? 1U : 0U);

        shared->needed_offsets =
            calloc(maximum_needed == 0U ? 1U : maximum_needed,
                   sizeof(*shared->needed_offsets));
    }
    if (shared->dynsym_index == NULL ||
        shared->dynstr_name_offset == NULL ||
        shared->plt_index == NULL ||
        shared->got_slot_offset == NULL ||
        shared->tls_gd_offset == NULL ||
        shared->preemptible == NULL ||
        shared->needed_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-symbol-map\n");
        goto fail;
    }
    for (i = 0U; i < shared->symbol_map_capacity; ++i) {
        shared->dynsym_index[i] = SIZE_MAX;
        shared->plt_index[i] = SIZE_MAX;
        shared->got_slot_offset[i] = SIZE_MAX;
        shared->tls_gd_offset[i] = SIZE_MAX;
    }
    if (!shared_load_dynamic_list(state, shared, dynamic_list_path)) {
        goto fail;
    }

    if (!find_or_add_section(state,
                             ".dynstr",
                             SHT_STRTAB,
                             SHF_ALLOC,
                             1U,
                             0U,
                             &shared->dynstr_section) ||
        !find_or_add_section(state,
                             ".dynsym",
                             SHT_DYNSYM,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Sym),
                             &shared->dynsym_section) ||
        !find_or_add_section(state,
                             ".hash",
                             SHT_HASH,
                             SHF_ALLOC,
                             4U,
                             sizeof(uint32_t),
                             &shared->hash_section) ||
        !find_or_add_section(state,
                             ".rela.dyn",
                             SHT_RELA,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Rela),
                             &shared->rela_section) ||
        !find_or_add_section(state,
                             ".got",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             8U,
                             &shared->got_section) ||
        !find_or_add_section(state,
                             ".plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_EXECINSTR,
                             16U,
                             16U,
                             &shared->plt_section) ||
        !find_or_add_section(state,
                             ".got.plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             8U,
                             &shared->gotplt_section) ||
        !find_or_add_section(state,
                             ".rela.plt",
                             SHT_RELA,
                             SHF_ALLOC,
                             8U,
                             sizeof(Elf64_Rela),
                             &shared->rela_plt_section) ||
        !find_or_add_section(state,
                             ".dynamic",
                             SHT_DYNAMIC,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             sizeof(Elf64_Dyn),
                             &shared->dynamic_section)) {
        goto fail;
    }
    if (hash_style_both &&
        !find_or_add_section(state, ".gnu.hash", SHT_GNU_HASH,
                             SHF_ALLOC, 8U, 0U,
                             &shared->gnu_hash_section)) {
        goto fail;
    }
    if (version_script != NULL &&
        (!find_or_add_section(state, ".gnu.version",
                               SHT_GNU_versym, SHF_ALLOC, 2U, 2U,
                               &shared->versym_section) ||
         !find_or_add_section(state, ".gnu.version_d",
                               SHT_GNU_verdef, SHF_ALLOC, 4U, 0U,
                               &shared->verdef_section))) {
        goto fail;
    }
    if (interpreter_path != NULL && interpreter_path[0] != '\0') {
        if (!find_or_add_section(state,
                                 ".interp",
                                 SHT_PROGBITS,
                                 SHF_ALLOC,
                                 1U,
                                 0U,
                                 &shared->interp_section) ||
            !section_append_data(
                &state->work.sections[shared->interp_section],
                (const unsigned char *)interpreter_path,
                strlen(interpreter_path) + 1U)) {
            goto oom;
        }
        shared->have_interp = true;
    }

    dynstr = &state->work.sections[shared->dynstr_section];
    dynsym = &state->work.sections[shared->dynsym_section];
    hash = &state->work.sections[shared->hash_section];
    rela = &state->work.sections[shared->rela_section];
    got = &state->work.sections[shared->got_section];
    plt = &state->work.sections[shared->plt_section];
    gotplt = &state->work.sections[shared->gotplt_section];
    rela_plt = &state->work.sections[shared->rela_plt_section];
    dynamic = &state->work.sections[shared->dynamic_section];

    if (!shared_define_linker_symbols_pre_layout(
            state,
            shared->dynamic_section)) {
        goto fail;
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        shared->preemptible[i] =
            shared_symbol_preemptible(state, shared, i) &&
            (version_script == NULL ||
             (state->resolver.entries[i].section == MINILD_SECTION_UNDEF &&
              !state->resolver.entries[i].script_planned_defined) ||
             shared_version_exports(version_script,
                                    state->resolver.entries[i].name));
    }

    if (!section_append_zero(dynstr, 1U)) {
        goto oom;
    }
    if (soname != NULL && soname[0] != '\0') {
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->soname_offset = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)soname,
                                 strlen(soname) + 1U)) {
            goto oom;
        }
        shared->have_soname = true;
    }
    if (needed_name != NULL && needed_name[0] != '\0') {
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->needed_offsets[shared->needed_count++] =
            (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)needed_name,
                                 strlen(needed_name) + 1U)) {
            goto oom;
        }
    }
    for (i = 0U; i < input_graph_dso_needed_count(&state->inputs); ++i) {
        const char *name = input_graph_dso_needed_name(&state->inputs, i);

        if (needed_name != NULL && strcmp(needed_name, name) == 0) {
            continue;
        }
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->needed_offsets[shared->needed_count++] =
            (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)name,
                                 strlen(name) + 1U)) {
            goto oom;
        }
    }

    if (version_script != NULL) {
        if (dynstr->size > UINT32_MAX ||
            !section_append_data(dynstr,
                                  (const unsigned char *)version_script->version_name,
                                  strlen(version_script->version_name) + 1U)) {
            goto oom;
        }
        shared->version_name_offset =
            (uint32_t)(dynstr->size -
                       strlen(version_script->version_name) - 1U);
    }
    shared->dynsym_count = 1U;
    for (i = 0U; i < state->resolver.count; ++i) {
        if (!shared_symbol_eligible(state, i, version_script)) {
            continue;
        }
        if (dynstr->size > UINT32_MAX) {
            goto oom;
        }
        shared->dynstr_name_offset[i] = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)state->resolver.entries[i].name,
                                 strlen(state->resolver.entries[i].name) + 1U)) {
            goto oom;
        }
        shared->dynsym_index[i] = shared->dynsym_count++;
    }

    if (version_script != NULL &&
        (shared->dynsym_count > SIZE_MAX / sizeof(Elf64_Half) ||
         !section_append_zero(&state->work.sections[shared->versym_section],
                              shared->dynsym_count * sizeof(Elf64_Half)) ||
         !section_append_zero(&state->work.sections[shared->verdef_section],
                              2U * (sizeof(Elf64_Verdef) +
                                    sizeof(Elf64_Verdaux))))) {
        goto oom;
    }

    if (shared->dynsym_count > SIZE_MAX / sizeof(Elf64_Sym)) {
        goto oom;
    }
    dynsym_size = shared->dynsym_count * sizeof(Elf64_Sym);
    if (!section_append_zero(dynsym, dynsym_size)) {
        goto oom;
    }

    {
        size_t nbucket = shared->dynsym_count < 4U
                             ? 1U
                             : shared->dynsym_count / 4U;
        if (nbucket == 0U) {
            nbucket = 1U;
        }
        if (nbucket > SIZE_MAX - shared->dynsym_count - 2U) {
            goto oom;
        }
        hash_words = 2U + nbucket + shared->dynsym_count;
    }
    if (hash_words > SIZE_MAX / sizeof(uint32_t) ||
        !section_append_zero(hash, hash_words * sizeof(uint32_t))) {
        goto oom;
    }

    if (shared->have_gnu_hash) {
        size_t count = shared->dynsym_count;
        /* Header(16) + bloom(8) + bucket(4) + 4*(N-1) chain. */
        if (count > (SIZE_MAX - 24U) / 4U ||
            !section_append_zero(&state->work.sections[shared->gnu_hash_section],
                                 24U + count * 4U)) {
            goto oom;
        }
    }
    {
        MiniLdSharedRelocScanResult scan =
            shared_scan_relocation_requirements(state, shared);

        if (scan == MINILD_SHARED_RELOC_SCAN_OOM) {
            goto oom;
        }
        if (scan != MINILD_SHARED_RELOC_SCAN_OK) {
            goto fail;
        }
    }
    if (shared->got_count > SIZE_MAX / 8U ||
        shared->tls_gd_count > SIZE_MAX / 16U ||
        shared->got_count * 8U >
            SIZE_MAX - shared->tls_gd_count * 16U) {
        goto oom;
    }
    if (shared->tls_gd_count != 0U) {
        size_t got_prefix = shared->got_count * 8U;

        for (i = 0U; i < state->resolver.count; ++i) {
            if (shared->tls_gd_offset[i] != SIZE_MAX) {
                shared->tls_gd_offset[i] += got_prefix;
            }
        }
    }
    if ((shared->got_count != 0U || shared->tls_gd_count != 0U) &&
        !section_append_zero(got,
                             shared->got_count * 8U +
                                 shared->tls_gd_count * 16U)) {
        goto oom;
    }
    if (shared->rela_count > SIZE_MAX / sizeof(Elf64_Rela) ||
        !section_append_zero(rela,
                             shared->rela_count * sizeof(Elf64_Rela))) {
        goto oom;
    }

    if (shared->plt_count != 0U) {
        size_t plt_size;
        size_t gotplt_size;
        size_t rela_plt_size;

        if (shared->plt_count > (SIZE_MAX - 32U) / 16U ||
            shared->plt_count > (SIZE_MAX - 16U) / 8U ||
            shared->plt_count > SIZE_MAX / sizeof(Elf64_Rela)) {
            goto oom;
        }
        plt_size = shared->plt_count * 16U;
        gotplt_size = (3U + shared->plt_count) * 8U;
        rela_plt_size = shared->plt_count * sizeof(Elf64_Rela);
        if (!section_append_zero(plt, plt_size) ||
            !section_append_zero(gotplt, gotplt_size) ||
            !section_append_zero(rela_plt, rela_plt_size)) {
            goto oom;
        }
    }

    dynamic_entries = 6U; /* HASH/STRTAB/SYMTAB/STRSZ/SYMENT/NULL */
    if (shared->have_gnu_hash) {
        ++dynamic_entries;
    }
    if (version_script != NULL) {
        dynamic_entries += 3U; /* VERSYM/VERDEF/VERDEFNUM */
    }
    if (shared->rela_count != 0U) {
        dynamic_entries += 3U; /* RELA/RELASZ/RELAENT */
    }
    if (shared->have_textrel) {
        dynamic_entries += 2U; /* TEXTREL/FLAGS(DF_TEXTREL) */
    }
    if (shared->plt_count != 0U) {
        dynamic_entries += 5U; /* PLTGOT/PLTRELSZ/PLTREL/JMPREL/BIND_NOW */
    }
    if (shared->have_soname) {
        ++dynamic_entries;
    }
    if (shared->needed_count > SIZE_MAX - dynamic_entries) {
        goto oom;
    }
    dynamic_entries += shared->needed_count;
    if (shared->pie) {
        ++dynamic_entries; /* FLAGS_1 */
    }
    if (dynamic_entries > SIZE_MAX / sizeof(Elf64_Dyn) ||
        !section_append_zero(dynamic,
                             dynamic_entries * sizeof(Elf64_Dyn))) {
        goto oom;
    }

    if (eh_frame_hdr && !minild_eh_prepare(state, shared)) {
        goto fail;
    }
    return true;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-metadata\n");
fail:
    shared_image_destroy(shared);
    return false;
}

static bool shared_fill_dynsym(MiniLdState *state,
                               const MiniLdLayout *layout,
                               const MiniLdOutputPlan *output_plan,
                               const MiniLdSharedImage *shared) {
    MiniLdSection *dynsym = &state->work.sections[shared->dynsym_section];
    size_t i;

    memset(dynsym->data, 0, dynsym->size);
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *input = &state->resolver.entries[i];
        size_t dynamic_index = shared->dynsym_index[i];
        Elf64_Sym output;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        memset(&output, 0, sizeof(output));
        output.st_name = shared->dynstr_name_offset[i];
        output.st_info = input->info;
        output.st_other = input->other;
        output.st_size = input->size;
        if (input->section == MINILD_SECTION_UNDEF) {
            output.st_shndx = SHN_UNDEF;
            output.st_value = 0U;
        } else if (input->section == MINILD_SECTION_ABS) {
            output.st_shndx = SHN_ABS;
            output.st_value = input->value;
        } else if (input->section >= 0 &&
                   (size_t)input->section < state->work.count) {
            if (output_plan->work_shndx[input->section] == 0U) {
                output.st_shndx = SHN_UNDEF;
                memcpy(dynsym->data + dynamic_index * sizeof(output),
                       &output, sizeof(output));
                continue;
            }
            output.st_shndx =
                (Elf64_Section)output_plan->work_shndx[input->section];
            if (ELF64_ST_TYPE(input->info) == STT_TLS) {
                uint64_t tls_base = 0U;
                bool tls_present = false;

                if (!minild_final_tls_base_address(state,
                                                    layout,
                                                    output_plan,
                                                    &tls_base,
                                                    &tls_present) ||
                    !tls_present ||
                    layout->work[input->section].vaddr + input->value <
                        tls_base) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-shared-tls-symbol:%s\n",
                            input->name);
                    return false;
                }
                output.st_value =
                    layout->work[input->section].vaddr +
                    input->value - tls_base;
            } else {
                output.st_value =
                    layout->work[input->section].vaddr + input->value;
            }
        } else {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-symbol:%s\n",
                    input->name);
            return false;
        }
        memcpy(dynsym->data + dynamic_index * sizeof(output),
               &output,
               sizeof(output));
    }
    return true;
}

static bool shared_fill_hash(MiniLdState *state,
                             const MiniLdSharedImage *shared) {
    MiniLdSection *hash = &state->work.sections[shared->hash_section];
    size_t nbucket = shared->dynsym_count < 4U
                         ? 1U
                         : shared->dynsym_count / 4U;
    uint32_t *words = (uint32_t *)(void *)hash->data;
    uint32_t *buckets;
    uint32_t *chains;
    size_t i;

    if (nbucket == 0U) {
        nbucket = 1U;
    }
    memset(hash->data, 0, hash->size);
    words[0] = (uint32_t)nbucket;
    words[1] = (uint32_t)shared->dynsym_count;
    buckets = words + 2U;
    chains = buckets + nbucket;

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t dynamic_index = shared->dynsym_index[i];
        uint32_t bucket;
        uint32_t *slot;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        bucket = shared_elf_hash(state->resolver.entries[i].name) % (uint32_t)nbucket;
        slot = &buckets[bucket];
        if (*slot == 0U) {
            *slot = (uint32_t)dynamic_index;
        } else {
            uint32_t cursor = *slot;
            while (chains[cursor] != 0U) {
                cursor = chains[cursor];
            }
            chains[cursor] = (uint32_t)dynamic_index;
        }
    }
    return true;
}

/* ABI-correct one-bucket ELF64 GNU hash. The pre-existing dynsym indices
 * 1..N-1 are contiguous, allowing the complete chain without reordering. */
static bool shared_fill_gnu_hash(MiniLdState *state,
                                 const MiniLdSharedImage *shared) {
    MiniLdSection *section;
    uint64_t bloom = 0U;
    size_t i;

    if (!shared->have_gnu_hash) {
        return true;
    }
    section = &state->work.sections[shared->gnu_hash_section];
    if (shared->dynsym_count == 0U ||
        shared->dynsym_count > UINT32_MAX ||
        shared->dynsym_count > (SIZE_MAX - 24U) / 4U ||
        section->size != 24U + 4U * shared->dynsym_count) {
        fprintf(state->diagnostics, "minic-ld: invalid-gnu-hash-layout\n");
        return false;
    }
    memset(section->data, 0, section->size);
    store_u32le(section->data + 0U, 1U);
    store_u32le(section->data + 4U, 1U);
    store_u32le(section->data + 8U, 1U);
    store_u32le(section->data + 12U, 5U);
    store_u32le(section->data + 24U,
                shared->dynsym_count == 1U ? 0U : 1U);
    for (i = 0U; i < state->resolver.count; ++i) {
        size_t index = shared->dynsym_index[i];
        uint32_t hash;
        if (index == SIZE_MAX) {
            continue;
        }
        if (index == 0U || index >= shared->dynsym_count) {
            fprintf(state->diagnostics, "minic-ld: invalid-gnu-hash-symbol\n");
            return false;
        }
        hash = shared_gnu_symbol_hash(state->resolver.entries[i].name);
        bloom |= UINT64_C(1) << (hash % 64U);
        bloom |= UINT64_C(1) << ((hash >> 5U) % 64U);
        store_u32le(section->data + 28U + (index - 1U) * 4U,
                    (hash & ~UINT32_C(1)) |
                    (index + 1U == shared->dynsym_count ? 1U : 0U));
    }
    store_u64le(section->data + 16U, bloom);
    return true;
}

#include "linker_version.inc"

/* Both relocation passes must use the same final membership decision as
 * symbol and section-table emission.  In particular, a surviving non-ALLOC
 * input still needs link-time fixups, but must never acquire a DT_RELA. */
#include "linker_shared_reloc_apply.inc"

static bool shared_fill_dynamic(MiniLdState *state,
                                const MiniLdLayout *layout,
                                const MiniLdSharedImage *shared) {
    MiniLdSection *dynamic = &state->work.sections[shared->dynamic_section];
    Elf64_Dyn *entries = (Elf64_Dyn *)(void *)dynamic->data;
    size_t index = 0U;

#define MINILD_DYN(TAG, VALUE)          \
    do {                                \
        entries[index].d_tag = (TAG);   \
        entries[index].d_un.d_val = (VALUE); \
        ++index;                        \
    } while (0)

    memset(dynamic->data, 0, dynamic->size);
    MINILD_DYN(DT_HASH, layout->work[shared->hash_section].vaddr);
    if (shared->have_gnu_hash) {
        MINILD_DYN(DT_GNU_HASH, layout->work[shared->gnu_hash_section].vaddr);
    }
    if (shared->version_script != NULL) {
        MINILD_DYN(DT_VERSYM, layout->work[shared->versym_section].vaddr);
        MINILD_DYN(DT_VERDEF, layout->work[shared->verdef_section].vaddr);
        MINILD_DYN(DT_VERDEFNUM, 2U);
    }
    MINILD_DYN(DT_STRTAB, layout->work[shared->dynstr_section].vaddr);
    MINILD_DYN(DT_SYMTAB, layout->work[shared->dynsym_section].vaddr);
    MINILD_DYN(DT_STRSZ, state->work.sections[shared->dynstr_section].size);
    MINILD_DYN(DT_SYMENT, sizeof(Elf64_Sym));
    if (shared->rela_count != 0U) {
        MINILD_DYN(DT_RELA, layout->work[shared->rela_section].vaddr);
        MINILD_DYN(DT_RELASZ, state->work.sections[shared->rela_section].size);
        MINILD_DYN(DT_RELAENT, sizeof(Elf64_Rela));
    }
    if (shared->have_textrel) {
        MINILD_DYN(DT_TEXTREL, 0U);
#ifdef DF_TEXTREL
        MINILD_DYN(DT_FLAGS, DF_TEXTREL);
#else
        MINILD_DYN(DT_FLAGS, UINT64_C(0x00000004)); /* ELF DF_TEXTREL */
#endif
    }
    if (shared->plt_count != 0U) {
        MINILD_DYN(DT_PLTGOT, layout->work[shared->gotplt_section].vaddr);
        MINILD_DYN(DT_PLTRELSZ, state->work.sections[shared->rela_plt_section].size);
        MINILD_DYN(DT_PLTREL, DT_RELA);
        MINILD_DYN(DT_JMPREL, layout->work[shared->rela_plt_section].vaddr);
        MINILD_DYN(DT_BIND_NOW, 0U);
    }
    {
        size_t needed_index;

        for (needed_index = 0U;
             needed_index < shared->needed_count;
             ++needed_index) {
            MINILD_DYN(DT_NEEDED, shared->needed_offsets[needed_index]);
        }
    }
    if (shared->have_soname) {
        MINILD_DYN(DT_SONAME, shared->soname_offset);
    }
    if (shared->pie) {
        MINILD_DYN(DT_FLAGS_1, UINT64_C(0x08000000)); /* DF_1_PIE */
    }
    MINILD_DYN(DT_NULL, 0U);

#undef MINILD_DYN

    if (index * sizeof(Elf64_Dyn) != dynamic->size) {
        fprintf(state->diagnostics,
                "minic-ld: shared-dynamic-size-mismatch\n");
        return false;
    }
    return true;
}


typedef struct MiniLdSharedTlsSegment {
    bool present;
    size_t file_offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} MiniLdSharedTlsSegment;

static bool shared_tls_segment(const MiniLdState *state,
                               const MiniLdLayout *layout,
                               const MiniLdOutputPlan *output_plan,
                               MiniLdSharedTlsSegment *tls) {
    uint64_t base = UINT64_MAX;
    uint64_t memory_end = 0U;
    uint64_t file_end = 0U;
    uint64_t alignment = 1U;
    size_t i;
    bool have_file = false;
    bool have_nonempty = false;

    memset(tls, 0, sizeof(*tls));
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t start;
        uint64_t end;

        /* Final section membership, not the original SHF_TLS bit, decides
         * whether an input can contribute to PT_TLS.  In particular a
         * linker-script /DISCARD/ input has already been pruned before
         * layout and can retain its original flags with size zero. */
        if ((section->flags & (SHF_ALLOC | SHF_TLS)) !=
                (SHF_ALLOC | SHF_TLS) ||
            minild_output_shndx(output_plan, i) == 0U) {
            continue;
        }
        /* Keep surviving empty TLS sections in the base calculation to
         * agree with the final TLS-symbol view, but do not emit PT_TLS
         * when every surviving TLS fragment is empty. */
        have_nonempty |= section->size != 0U;
        start = layout->work[i].vaddr;
        if (section->size > UINT64_MAX - start) {
            return false;
        }
        end = start + (uint64_t)section->size;
        if (!tls->present || start < base) {
            base = start;
        }
        if (!tls->present || end > memory_end) {
            memory_end = end;
        }
        if (section->align > alignment) {
            alignment = section->align;
        }
        if (section->type != SHT_NOBITS && section->size != 0U) {
            if (layout->work[i].file_offset == SIZE_MAX) {
                return false;
            }
            if (!have_file || end > file_end) {
                file_end = end;
            }
            have_file = true;
        }
        tls->present = true;
    }

    if (!have_nonempty) {
        tls->present = false;
        return true;
    }
    if (!layout->have_rw || base < layout->rw_vaddr ||
        base - layout->rw_vaddr > SIZE_MAX ||
        layout->rw_file_offset >
            SIZE_MAX - (size_t)(base - layout->rw_vaddr)) {
        return false;
    }

    tls->vaddr = base;
    tls->file_offset =
        layout->rw_file_offset + (size_t)(base - layout->rw_vaddr);
    tls->filesz = have_file ? file_end - base : 0U;
    tls->memsz = memory_end - base;
    tls->align = alignment == 0U ? 1U : alignment;
    return true;
}

/* The ET_DYN segment policy owns the program headers before serialization.
 * PT_LOAD includes ELF headers; PIE, TLS and vDSO introduce distinct PHDRs. */
static bool minild_shared_program_plan_build(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared,
    MiniLdProgramPlan *plan) {
    MiniLdSharedTlsSegment tls;
    size_t phnum;
    size_t program_index = 0U;
    size_t i;
    Elf64_Phdr *programs;

    memset(plan, 0, sizeof(*plan));
    if (!shared_tls_segment(state, layout, output_plan, &tls)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-tls-segment-layout\n");
        return false;
    }
    phnum = (shared->have_eh_frame_hdr ||
             shared->layout_script != NULL ? 1U : 0U) +
            (shared->layout_script != NULL ? 1U : 0U) +
            (shared->pie ? 1U : 0U) +
            (shared->have_interp ? 1U : 0U) +
            (layout->have_rx ? 1U : 0U) +
            (layout->have_rw ? 1U : 0U) +
            (tls.present ? 1U : 0U) + 1U;
    if (phnum > UINT16_MAX ||
        phnum > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr)) {
        fprintf(state->diagnostics, "minic-ld: too-many-shared-segments\n");
        return false;
    }
    plan->entries = calloc(phnum, sizeof(*plan->entries));
    if (plan->entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-segments\n");
        return false;
    }
    plan->count = phnum;
    programs = plan->entries;

    if (shared->pie) {
        Elf64_Phdr *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if (!layout->have_rx ||
            (uint64_t)layout->rx_file_offset > layout->rx_vaddr) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-phdr-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->p_type = PT_PHDR;
        ph->p_flags = PF_R;
        ph->p_offset = sizeof(Elf64_Ehdr);
        ph->p_vaddr = header_vaddr + sizeof(Elf64_Ehdr);
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = phnum * sizeof(Elf64_Phdr);
        ph->p_memsz = ph->p_filesz;
        ph->p_align = sizeof(Elf64_Addr);
    }
    if (shared->have_interp) {
        Elf64_Phdr *ph = &programs[program_index++];
        const MiniLdSection *interp;

        if (shared->interp_section >= state->work.count ||
            layout->work[shared->interp_section].file_offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-interpreter-layout\n");
            goto failed;
        }
        interp = &state->work.sections[shared->interp_section];
        ph->p_type = PT_INTERP;
        ph->p_flags = PF_R;
        ph->p_offset = layout->work[shared->interp_section].file_offset;
        ph->p_vaddr = layout->work[shared->interp_section].vaddr;
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = interp->size;
        ph->p_memsz = interp->size;
        ph->p_align = 1U;
    }
    if (layout->have_rx) {
        Elf64_Phdr *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if ((uint64_t)layout->rx_file_offset > layout->rx_vaddr ||
            layout->rx_file_offset >
                SIZE_MAX - layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-header-load-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_X;
        ph->p_offset = 0U;
        ph->p_vaddr = header_vaddr;
        ph->p_paddr = header_vaddr;
        ph->p_filesz =
            (uint64_t)(layout->rx_file_offset + layout->rx_file_size);
        ph->p_memsz = ph->p_filesz;
        ph->p_align = 4096U;
    }
    if (layout->have_rw) {
        Elf64_Phdr *ph = &programs[program_index++];
        ph->p_type = PT_LOAD;
        ph->p_flags = PF_R | PF_W;
        ph->p_offset = layout->rw_file_offset;
        ph->p_vaddr = layout->rw_vaddr;
        ph->p_paddr = layout->rw_vaddr;
        ph->p_filesz = layout->rw_file_size;
        ph->p_memsz = layout->rw_mem_size;
        ph->p_align = 4096U;
    }
    if (tls.present) {
        Elf64_Phdr *ph = &programs[program_index++];
        ph->p_type = PT_TLS;
        ph->p_flags = PF_R;
        ph->p_offset = tls.file_offset;
        ph->p_vaddr = tls.vaddr;
        ph->p_paddr = tls.vaddr;
        ph->p_filesz = tls.filesz;
        ph->p_memsz = tls.memsz;
        ph->p_align = tls.align;
    }
    {
        Elf64_Phdr *ph = &programs[program_index++];
        MiniLdSection *dynamic = &state->work.sections[shared->dynamic_section];
        size_t offset = layout->work[shared->dynamic_section].file_offset;

        if (offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-dynamic-file-offset\n");
            goto failed;
        }
        ph->p_type = PT_DYNAMIC;
        ph->p_flags = shared->layout_script != NULL ? PF_R : PF_R | PF_W;
        ph->p_offset = offset;
        ph->p_vaddr = layout->work[shared->dynamic_section].vaddr;
        ph->p_paddr = ph->p_vaddr;
        ph->p_filesz = dynamic->size;
        ph->p_memsz = dynamic->size;
        ph->p_align = 8U;
    }

    if (shared->layout_script != NULL) {
        /* Emit only the actual GNU vDSO mapped PT_NOTE section(s).
         * A missing or out-of-load note is a hard failure. */
        size_t note_offset = SIZE_MAX;
        uint64_t note_addr = UINT64_MAX;
        uint64_t note_end = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            size_t offset = layout->work[i].file_offset;
            uint64_t address = layout->work[i].vaddr;
            if (section->type != SHT_NOTE ||
                (section->flags & SHF_ALLOC) == 0U ||
                layout->work[i].discarded || section->size == 0U) {
                continue;
            }
            if (offset == SIZE_MAX ||
                address > UINT64_MAX - section->size ||
                offset > SIZE_MAX - section->size) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-vdso-script-note-placement\n");
                goto failed;
            }
            if (offset < note_offset) {
                note_offset = offset;
            }
            if (address < note_addr) {
                note_addr = address;
            }
            if (address + section->size > note_end) {
                note_end = address + section->size;
            }
        }
        if (note_offset == SIZE_MAX || note_addr == UINT64_MAX ||
            note_end <= note_addr ||
            note_end - note_addr > layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-vdso-script-PT_NOTE\n");
            goto failed;
        }
        {
            Elf64_Phdr *ph = &programs[program_index++];
            ph->p_type = PT_NOTE;
            ph->p_flags = PF_R;
            ph->p_offset = note_offset;
            ph->p_vaddr = note_addr;
            ph->p_paddr = note_addr;
            ph->p_filesz = note_end - note_addr;
            ph->p_memsz = ph->p_filesz;
            ph->p_align = 4U;
        }
    }
    if (shared->have_eh_frame_hdr ||
        shared->layout_script != NULL) {
        Elf64_Phdr *ph = &programs[program_index++];
        memset(ph, 0, sizeof(*ph));
        ph->p_type = PT_GNU_EH_FRAME;
        if (shared->have_eh_frame_hdr) {
            size_t h = shared->eh_frame_hdr_section;
            if (h >= state->work.count ||
                layout->work[h].file_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-eh-frame-hdr-file-offset\n");
                goto failed;
            }
            ph->p_flags = PF_R;
            ph->p_offset = layout->work[h].file_offset;
            ph->p_vaddr = layout->work[h].vaddr;
            ph->p_paddr = ph->p_vaddr;
            ph->p_filesz = state->work.sections[h].size;
            ph->p_memsz = ph->p_filesz;
            ph->p_align = 4U;
        } else {
            /* GNU's explicit-script header when all vDSO CFI FDEs
             * disappear: 0-byte PT_GNU_EH_FRAME, no invented section. */
            ph->p_align = 8U;
        }
    }
    if (program_index != phnum) {
        fprintf(state->diagnostics, "minic-ld: shared-phdr-count-mismatch\n");
        goto failed;
    }
    return true;

failed:
    minild_program_plan_destroy(plan);
    return false;
}

/* Only ET_DYN synthetic sections add dynamic sh_link/sh_info policy.
 * All generic section identity and LINK_ORDER links are already finalized. */
static void minild_shared_output_section_links(
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared,
    Elf64_Shdr *section_headers) {
    size_t i;
    for (i = 0U; i < output_plan->count; ++i) {
        Elf64_Shdr *sh = &section_headers[i + 1U];

        if (i + 1U == minild_output_shndx(output_plan,
                                           shared->dynsym_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, shared->dynstr_section);
            sh->sh_info = 1U;
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->versym_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, shared->dynsym_section);
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->verdef_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, shared->dynstr_section);
            sh->sh_info = 2U;
        } else if (i + 1U == minild_output_shndx(output_plan,
                                                  shared->hash_section) ||
                   (shared->have_gnu_hash &&
                    i + 1U == minild_output_shndx(output_plan,
                                                   shared->gnu_hash_section)) ||
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->rela_section) ||
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->rela_plt_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, shared->dynsym_section);
        } else if (i + 1U == minild_output_shndx(output_plan,
                                                  shared->dynamic_section)) {
            sh->sh_link = (Elf64_Word)
                minild_output_shndx(output_plan, shared->dynstr_section);
        }
    }
}

static bool shared_write_object(MiniLdState *state,
                                const MiniLdLayout *layout,
                                MiniLdOutputPlan *output_plan,
                                const MiniLdSharedImage *shared,
                                const char *path,
                                uint64_t entry) {
    MiniLdFinalElf64Image final = {0};
    size_t image_size;
    bool ok = false;

    if (!minild_shared_program_plan_build(state, layout, output_plan, shared,
                                          &final.program)) {
        goto done;
    }
    image_size = sizeof(Elf64_Ehdr) +
                 final.program.count * sizeof(Elf64_Phdr);

    if (output_plan->count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-shared-sections\n");
        goto done;
    }
    if (layout->have_rx &&
        layout->rx_file_offset + layout->rx_file_size > image_size) {
        image_size = layout->rx_file_offset + layout->rx_file_size;
    }
    if (layout->have_rw &&
        layout->rw_file_offset + layout->rw_file_size > image_size) {
        image_size = layout->rw_file_offset + layout->rw_file_size;
    }
    /* GNU nm reads the final .symtab from the vDSO, not only .dynsym.
     * Synthetic non-ALLOC names and symbols are after the LOAD image.
     * Dynamic section-header links remain ET_DYN-specific policy. */
    if (!minild_final_elf64_image_prepare(state, layout, output_plan,
                                          &final, image_size,
                                          shared->version_script,
                                          MINILD_FINAL_STRTAB_FIRST,
                                          ET_DYN)) {
        goto done;
    }
    minild_shared_output_section_links(output_plan, shared,
                                        final.tables.headers);
    ok = minild_elf64_write_image(state, layout, output_plan, &final.program,
                                  &final.tables, &final.symbols,
                                  ET_DYN, entry, path);

done:
    /* The old writer removed partial output even when planning failed before
     * image allocation.  Preserve that fail-closed contract; the common ELF
     * serializer also removes any file it may have opened. */
    if (!ok) {
        (void)remove(path);
    }
    minild_final_elf64_image_destroy(&final);
    return ok;
}

static bool shared_validate_pie_dso_symbols(
    const MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        unsigned bind = ELF64_ST_BIND(symbol->info);

        if (symbol->section != MINILD_SECTION_UNDEF ||
            symbol->script_planned_defined ||
            bind == STB_WEAK ||
            bind == STB_LOCAL ||
            symbol->name[0] == '\0') {
            continue;
        }
        if (!state_dso_defines(state, symbol->name)) {
            fprintf(state->diagnostics,
                    "minic-ld: unresolved-pie-symbol:%s\n",
                    symbol->name);
            return false;
        }
    }
    return true;
}

/* -S on an already debug-free ELF shared input is a semantic no-op.
 * Reject unimplemented debug removal rather than silently emit a DSO
 * still containing non-ALLOC debugging sections. .eh_frame is runtime
 * unwind data and is intentionally not classified as debug information.
 */
static bool shared_strip_debug_preflight(const MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        const char *name = section->name;

        if (section->size == 0U) {
            continue;
        }
        if (strncmp(name, ".debug", 6U) == 0 ||
            strncmp(name, ".zdebug", 7U) == 0 ||
            strncmp(name, ".stab", 5U) == 0 ||
            strcmp(name, ".gdb_index") == 0 ||
            strcmp(name, ".gnu_debuglink") == 0 ||
            strcmp(name, ".gnu_debugaltlink") == 0) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-strip-debug-section:%s\n",
                    name);
            return false;
        }
    }
    return true;
}

/* Shared metadata/dynamic relocation slots are prepared before the vDSO
 * script places its inputs.  Until these builders consume the same liveness
 * map as the final OutputPlan, silently discarding an allocated source could
 * leave a dynamic relocation or exported symbol pointing into dead bytes.
 * Reject that exact unsupported case instead of emitting an invalid DSO. */
static bool shared_script_validate_discarded_alloc(
    const MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];

        if (layout->work[i].discarded &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-discard-alloc:%s\n",
                    script_section_source_name(section));
            return false;
        }
    }
    return true;
}

int minild_link_shared_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    FILE *diagnostics) {
    const char *soname = options != NULL ? options->soname : NULL;
    const char *entry_symbol =
        options != NULL ? options->entry_symbol : NULL;
    const char *dynamic_list_path =
        options != NULL ? options->dynamic_list_path : NULL;
    const char *version_script_path =
        options != NULL ? options->version_script_path : NULL;
    const char *script_path =
        options != NULL ? options->script_path : NULL;
    const char *needed_name =
        options != NULL ? options->needed_name : NULL;
    const bool pie = options != NULL && options->pie;
    const bool strip_debug = options != NULL && options->strip_debug;
    const bool hash_style_both = options != NULL && options->hash_style_both;
    const bool eh_frame_hdr = options != NULL && options->eh_frame_hdr;
    const char *interpreter_path =
        options != NULL && options->interpreter_path != NULL
            ? options->interpreter_path
            : pie ? "/lib/ld-musl-riscv64.so.1" : NULL;
    MiniLdState state;
    MiniLdLayout layout;
    MiniLdSharedImage shared;
    MiniLdOutputPlan output_plan = {0};
    MiniLdScript version_script;
    MiniLdScript layout_script;
    const MiniLdScript *version = NULL;
    uint64_t entry = 0U;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&shared, 0, sizeof(shared));
    minild_script_initialize(&version_script);
    minild_script_initialize(&layout_script);
    state.diagnostics = diagnostics;
    state.allow_dso_inputs = true;
    if (version_script_path != NULL) {
        if (!minild_script_parse_file(version_script_path,
                                      &version_script, diagnostics) ||
            version_script.version_name == NULL ||
            !version_script.version_local_all ||
            version_script.command_count != 0U ||
            version_script.phdr_count != 0U ||
            version_script.version_global_count == 0U ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-version-script\n");
            goto done;
        }
        version = &version_script;
    }
    if (script_path != NULL) {
        if (version != NULL ||
            !minild_script_parse_file(script_path,
                                      &layout_script, diagnostics) ||
            layout_script.version_name == NULL ||
            layout_script.version_global_count == 0U ||
            !layout_script.version_local_all ||
            layout_script.output_arch == NULL ||
            strcmp(layout_script.output_arch, "riscv") != 0 ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-script-contract:%s\n",
                    script_path);
            goto done;
        }
        version = &layout_script;
    }

    if (!process_input_sequence(&state, inputs, input_count) ||
        !state.have_input ||
        !minild_project_admitted_inputs(
            &state, script_path != NULL ? MINILD_PROJECTION_SCRIPT
                                        : MINILD_PROJECTION_MERGED) ||
        (script_path != NULL && !minild_restore_input_order(&state)) ||
        (strip_debug && !shared_strip_debug_preflight(&state)) ||
        !static_allocate_common(&state) ||
        (script_path != NULL &&
         !minild_shared_prune_script_inputs(&state, &layout_script)) ||
        (script_path != NULL &&
         !minild_script_declare_shared_symbols(&state, &layout_script)) ||
        !shared_prepare_metadata(&state,
                                 soname,
                                 dynamic_list_path,
                                 needed_name,
                                 interpreter_path,
                                 pie,
                                 hash_style_both,
                                 eh_frame_hdr,
                                 version,
                                 &shared) ||
        (pie && !shared_validate_pie_dso_symbols(&state)) ||
        !(script_path != NULL
              ? shared_build_vdso_script_layout(&state, &layout_script,
                                                &layout)
              : shared_build_layout(&state, &layout))) {
        goto done;
    }
    if (script_path != NULL &&
        !shared_script_validate_discarded_alloc(&state, &layout)) {
        goto done;
    }
    shared.layout_script = script_path != NULL ? &layout_script : NULL;
    if (state.resolver.count != shared.prepared_symbol_count) {
        fprintf(diagnostics,
                "minic-ld: script-symbol-count-changed:%zu:%zu\n",
                state.resolver.count, shared.prepared_symbol_count);
        goto done;
    }

    if (!minild_output_plan_build(&state, &layout, &output_plan) ||
        !shared_finalize_ehdr_symbol(&state, &layout) ||
        (entry_symbol != NULL &&
         !static_entry_address(&state, &layout, entry_symbol, &entry)) ||
        !shared_fill_dynsym(&state, &layout, &output_plan, &shared) ||
        !shared_fill_hash(&state, &shared) ||
        !shared_fill_gnu_hash(&state, &shared) ||
        !shared_fill_versions(&state, &shared) ||
        !shared_fill_relocations(&state, &layout, &output_plan, &shared) ||
        !shared_patch_final_layout_relocations(&state, &layout,
                                               &output_plan, &shared) ||
        !minild_eh_fill(&state, &layout, &shared) ||
        !shared_finalize_plt_and_calls(&state, &layout, &output_plan, &shared) ||
        !shared_fill_dynamic(&state, &layout, &shared) ||
        !shared_write_object(&state,
                             &layout,
                             &output_plan,
                             &shared,
                             output_path,
                             entry)) {
        goto done;
    }
    ok = true;

done:
    minild_output_plan_destroy(&output_plan);
    minild_layout_destroy(&layout);
    shared_image_destroy(&shared);
    state_destroy(&state);
    minild_script_destroy(&version_script);
    minild_script_destroy(&layout_script);
    return ok ? 0 : 1;
}

int minild_link_shared_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *soname,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdSharedOptions options;

    options.soname = soname;
    options.entry_symbol = entry_symbol;
    options.dynamic_list_path = NULL;
    options.version_script_path = NULL;
    options.script_path = NULL;
    options.needed_name = NULL;
    options.interpreter_path = NULL;
    options.strip_debug = false;
    options.hash_style_both = false;
    options.eh_frame_hdr = false;
    options.pie = false;
    return minild_link_shared_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}
