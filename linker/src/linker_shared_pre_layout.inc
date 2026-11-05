/* GNU ELF symbol-version table records have invariant wire layouts across
 * ELFCLASS32 and ELFCLASS64.  Reserve explicit on-disk bytes rather than
 * relying on native Elf64_* struct padding and host endianness. */
enum {
    MINILD_GNU_VERSYM_WIRE_BYTES = 2U,
    MINILD_GNU_VERDEF_WIRE_BYTES = 20U,
    MINILD_GNU_VERDAUX_WIRE_BYTES = 8U,
    MINILD_GNU_VERDEF_NODE_WIRE_BYTES =
        MINILD_GNU_VERDEF_WIRE_BYTES + MINILD_GNU_VERDAUX_WIRE_BYTES
};

/* One final-link projection per canonical resolver symbol.  Dynamic symbol
 * identity, preemption, and GOT/PLT/TLS reservations have one shared slot,
 * so no independently allocated symbol-indexed arrays can get out of sync. */
typedef struct MiniLdSharedSymbolPlan {
    size_t dynsym_index;
    size_t plt_index;
    size_t got_slot_offset;
    size_t tls_gd_offset;
    uint32_t dynstr_name_offset;
    bool preemptible;
} MiniLdSharedSymbolPlan;

/* ET_DYN pre-layout metadata and source-symbol policy. The one
 * MiniLdSharedImage is sized before address placement. */
typedef struct MiniLdSharedImage {
    const MiniLdSharedTargetAbi *abi;
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
    MiniLdSharedSymbolPlan *symbols;
    unsigned char *dynamic_list_data;
    size_t dynamic_list_size;
    size_t dynsym_count;
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
    free(shared->symbols);
    free(shared->needed_offsets);
    free(shared->dynamic_list_data);
    shared->symbols = NULL;
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
    return shared->symbols[index].preemptible;
}

/* A nonpreemptible SHN_ABS address is a load-independent constant.  Script
 * declarations can establish this fact before their values are evaluated,
 * so neither a dynamic RELATIVE relocation nor a loader textrel is needed. */
static bool shared_symbol_is_static_absolute(const MiniLdState *state,
                                             const MiniLdSharedImage *shared,
                                             size_t index) {
    const MiniLdResolvedSymbol *symbol;

    if (index >= state->resolver.count || shared->symbols[index].preemptible) {
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
    shared->abi = minild_shared_target_abi(state);
    if (shared->abi == NULL ||
        /* Shared metadata serializers still emit ELF64 wire records.
         * A distinct ELF32 ABI must bring real encoders before admission. */
        state->target == NULL ||
        state->target->elf_class != ELFCLASS64 ||
        shared->abi->dynsym_record_bytes != sizeof(Elf64_Sym) ||
        shared->abi->rela_record_bytes != sizeof(Elf64_Rela) ||
        shared->abi->dynamic_record_bytes != sizeof(Elf64_Dyn) ||
        shared->abi->got_slot_bytes == 0U ||
        shared->abi->tls_gd_slot_bytes == 0U ||
        shared->abi->plt_entry_bytes == 0U ||
        shared->abi->gotplt_slot_bytes == 0U ||
        shared->abi->classify_relocation == NULL ||
        shared->abi->relocation_at_final_layout == NULL ||
        shared->abi->dynamic_relocation_info == NULL ||
        shared->abi->addend_bits == NULL ||
        shared->abi->absolute_relocation_width == NULL ||
        shared->abi->store_absolute == NULL ||
        shared->abi->patch_final_relocations == NULL ||
        shared->abi->finalize_plt_and_calls == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-target-abi\n");
        return false;
    }
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
    if (shared->prepared_symbol_count >
        SIZE_MAX / sizeof(*shared->symbols)) {
        fprintf(state->diagnostics,
                "minic-ld: script-symbol-map-overflow\n");
        goto fail;
    }
    shared->symbols = calloc(shared->prepared_symbol_count == 0U
                                 ? 1U : shared->prepared_symbol_count,
                             sizeof(*shared->symbols));
    {
        size_t maximum_needed = input_graph_dso_needed_count(&state->inputs) +
            (needed_name != NULL && needed_name[0] != '\0' ? 1U : 0U);

        shared->needed_offsets =
            calloc(maximum_needed == 0U ? 1U : maximum_needed,
                   sizeof(*shared->needed_offsets));
    }
    if (shared->symbols == NULL ||
        shared->needed_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-symbol-map\n");
        goto fail;
    }
    for (i = 0U; i < shared->prepared_symbol_count; ++i) {
        shared->symbols[i].dynsym_index = SIZE_MAX;
        shared->symbols[i].plt_index = SIZE_MAX;
        shared->symbols[i].got_slot_offset = SIZE_MAX;
        shared->symbols[i].tls_gd_offset = SIZE_MAX;
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
                             shared->abi->dynsym_record_bytes,
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
                             shared->abi->rela_record_bytes,
                             &shared->rela_section) ||
        !find_or_add_section(state,
                             ".got",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             shared->abi->got_slot_bytes,
                             shared->abi->got_slot_bytes,
                             &shared->got_section) ||
        !find_or_add_section(state,
                             ".plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_EXECINSTR,
                             shared->abi->plt_entry_bytes,
                             shared->abi->plt_entry_bytes,
                             &shared->plt_section) ||
        !find_or_add_section(state,
                             ".got.plt",
                             SHT_PROGBITS,
                             SHF_ALLOC | SHF_WRITE,
                             shared->abi->gotplt_slot_bytes,
                             shared->abi->gotplt_slot_bytes,
                             &shared->gotplt_section) ||
        !find_or_add_section(state,
                             ".rela.plt",
                             SHT_RELA,
                             SHF_ALLOC,
                             8U,
                             shared->abi->rela_record_bytes,
                             &shared->rela_plt_section) ||
        !find_or_add_section(state,
                             ".dynamic",
                             SHT_DYNAMIC,
                             SHF_ALLOC | SHF_WRITE,
                             8U,
                             shared->abi->dynamic_record_bytes,
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
        shared->symbols[i].preemptible =
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
        shared->symbols[i].dynstr_name_offset = (uint32_t)dynstr->size;
        if (!section_append_data(dynstr,
                                 (const unsigned char *)state->resolver.entries[i].name,
                                 strlen(state->resolver.entries[i].name) + 1U)) {
            goto oom;
        }
        shared->symbols[i].dynsym_index = shared->dynsym_count++;
    }

    if (version_script != NULL &&
        (shared->dynsym_count > SIZE_MAX / MINILD_GNU_VERSYM_WIRE_BYTES ||
         !section_append_zero(&state->work.sections[shared->versym_section],
                              shared->dynsym_count * MINILD_GNU_VERSYM_WIRE_BYTES) ||
         !section_append_zero(&state->work.sections[shared->verdef_section],
                              2U * MINILD_GNU_VERDEF_NODE_WIRE_BYTES))) {
        goto oom;
    }

    if (shared->dynsym_count >
        SIZE_MAX / shared->abi->dynsym_record_bytes) {
        goto oom;
    }
    dynsym_size = shared->dynsym_count * shared->abi->dynsym_record_bytes;
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
    if (shared->got_count > SIZE_MAX / shared->abi->got_slot_bytes ||
        shared->tls_gd_count > SIZE_MAX / shared->abi->tls_gd_slot_bytes ||
        shared->got_count * shared->abi->got_slot_bytes >
            SIZE_MAX -
                shared->tls_gd_count * shared->abi->tls_gd_slot_bytes) {
        goto oom;
    }
    if (shared->tls_gd_count != 0U) {
        size_t got_prefix =
            shared->got_count * shared->abi->got_slot_bytes;

        for (i = 0U; i < state->resolver.count; ++i) {
            if (shared->symbols[i].tls_gd_offset != SIZE_MAX) {
                shared->symbols[i].tls_gd_offset += got_prefix;
            }
        }
    }
    if ((shared->got_count != 0U || shared->tls_gd_count != 0U) &&
        !section_append_zero(got,
                             shared->got_count * shared->abi->got_slot_bytes +
                                 shared->tls_gd_count *
                                     shared->abi->tls_gd_slot_bytes)) {
        goto oom;
    }
    if (shared->rela_count >
            SIZE_MAX / shared->abi->rela_record_bytes ||
        !section_append_zero(rela,
                             shared->rela_count *
                                 shared->abi->rela_record_bytes)) {
        goto oom;
    }

    if (shared->plt_count != 0U) {
        size_t plt_size;
        size_t gotplt_size;
        size_t rela_plt_size;

        if (shared->plt_count >
                SIZE_MAX / shared->abi->plt_entry_bytes ||
            shared->abi->gotplt_header_slots >
                SIZE_MAX / shared->abi->gotplt_slot_bytes ||
            shared->plt_count >
                SIZE_MAX / shared->abi->gotplt_slot_bytes -
                    shared->abi->gotplt_header_slots ||
            shared->plt_count >
                SIZE_MAX / shared->abi->rela_record_bytes) {
            goto oom;
        }
        plt_size = shared->plt_count * shared->abi->plt_entry_bytes;
        gotplt_size = (shared->abi->gotplt_header_slots + shared->plt_count) *
            shared->abi->gotplt_slot_bytes;
        rela_plt_size =
            shared->plt_count * shared->abi->rela_record_bytes;
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
    if (dynamic_entries >
            SIZE_MAX / shared->abi->dynamic_record_bytes ||
        !section_append_zero(dynamic,
                             dynamic_entries *
                                 shared->abi->dynamic_record_bytes)) {
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
