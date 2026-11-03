/* One final symbol-table owner for ET_EXEC and ET_DYN.  Input symbols and
 * their resolver indices are never reordered or annotated by the writer.
 * The final view owns the local-before-global order, SHN assignments,
 * relocated values and ELF string table; ET_REL keeps its source policy. */
typedef struct MiniLdFinalSymbols {
    MiniLdBuffer names;
    Elf64_Sym *entries;
    size_t entry_count;
    size_t local_count;
} MiniLdFinalSymbols;

static void minild_final_symbols_destroy(MiniLdFinalSymbols *final) {
    free(final->names.data);
    free(final->entries);
    memset(final, 0, sizeof(*final));
}

static bool minild_final_tls_base_address(const MiniLdState *state,
                                          const MiniLdLayout *layout,
                                          const MiniLdOutputPlan *plan,
                                          uint64_t *base_out,
                                          bool *present_out) {
    uint64_t base = UINT64_MAX;
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if ((section->flags & (SHF_ALLOC | SHF_TLS)) !=
                (SHF_ALLOC | SHF_TLS) ||
            plan->work_shndx[i] == 0U) {
            continue;
        }
        if (layout->work[i].vaddr < base) {
            base = layout->work[i].vaddr;
        }
    }
    *present_out = base != UINT64_MAX;
    *base_out = base == UINT64_MAX ? 0U : base;
    return true;
}

/* A VERSION { global: ...; local: *; } script hides final definitions
 * from the dynamic interface by making them local in the ordinary symbol
 * table too.  Compute that final ELF binding without changing canonical
 * resolver entries (whose indices are used by relocation and dynsym maps).
 * Undefined imports remain undefined globals: local: * only controls
 * definitions owned by the output. */
static bool minild_final_version_exports(const MiniLdScript *version,
                                         const char *name) {
    size_t i;

    for (i = 0U; i < version->version_global_count; ++i) {
        if (strcmp(version->version_globals[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool minild_final_symbol_local(const MiniLdResolvedSymbol *source,
                                      const MiniLdState *state,
                                      const MiniLdOutputPlan *plan,
                                      const MiniLdScript *version) {
    unsigned visibility;

    if (ELF64_ST_BIND(source->info) == STB_LOCAL) {
        return true;
    }
    if (version == NULL || !version->version_local_all ||
        source->section == MINILD_SECTION_UNDEF ||
        source->name[0] == '\0') {
        return false;
    }
    if (source->section >= 0 &&
        ((size_t)source->section >= state->work.count ||
         plan->work_shndx[source->section] == 0U)) {
        return false;
    }
    visibility = ELF64_ST_VISIBILITY(source->other);
    /* GNU ld localizes HIDDEN/INTERNAL definitions even if their names are
     * listed under global:.  PROTECTED definitions may be exported and
     * retain that visibility when explicitly whitelisted. */
    return (visibility != STV_DEFAULT && visibility != STV_PROTECTED) ||
           !minild_final_version_exports(version, source->name);
}

static bool minild_final_symbols_build_with_version(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *plan,
    const MiniLdScript *version_script,
    MiniLdFinalSymbols *final) {
    uint64_t tls_base = 0U;
    bool have_tls = false;
    size_t next = 1U;
    size_t pass, i;

    if (state->resolver.count >= SIZE_MAX / sizeof(Elf64_Sym) - 1U ||
        !minild_final_tls_base_address(state, layout, plan,
                                       &tls_base, &have_tls)) {
        return false;
    }
    final->entry_count = state->resolver.count + 1U;
    final->entries = calloc(final->entry_count,
                            sizeof(*final->entries));
    if (final->entries == NULL || !buffer_append_zero(&final->names, 1U)) {
        return false;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < state->resolver.count; ++i) {
            const MiniLdResolvedSymbol *source = &state->resolver.entries[i];
            bool local = minild_final_symbol_local(source, state, plan,
                                                   version_script);
            bool localized = local &&
                             ELF64_ST_BIND(source->info) != STB_LOCAL;
            Elf64_Sym *output;

            if (local != (pass == 0U)) {
                continue;
            }
            output = &final->entries[next++];
            if (source->name[0] != '\0' &&
                !buffer_append_string(&final->names, source->name,
                                      &output->st_name)) {
                return false;
            }
            output->st_info = localized
                                  ? ELF64_ST_INFO(STB_LOCAL,
                                                  ELF64_ST_TYPE(source->info))
                                  : source->info;
            /* GNU's localized .symtab entries use DEFAULT visibility;
             * preserve input locals and target-specific high st_other bits. */
            output->st_other = localized
                                   ? (unsigned char)(source->other & ~3U)
                                   : source->other;
            output->st_size = source->size;
            if (source->section == MINILD_SECTION_UNDEF) {
                output->st_shndx = SHN_UNDEF;
            } else if (source->section == MINILD_SECTION_ABS) {
                output->st_shndx = SHN_ABS;
                output->st_value = source->value;
            } else if (source->section == MINILD_SECTION_COMMON) {
                output->st_shndx = SHN_COMMON;
                output->st_value = source->value;
            } else if (source->section >= 0 &&
                       (size_t)source->section < state->work.count) {
                size_t section = (size_t)source->section;
                if (plan->work_shndx[section] == 0U) {
                    output->st_shndx = SHN_UNDEF;
                } else {
                    output->st_shndx =
                        (Elf64_Half)plan->work_shndx[section];
                    if (ELF64_ST_TYPE(source->info) == STT_TLS) {
                        /* A final TLS symbol is relative to the TLS block,
                         * not the section VMA (ET_EXEC and ET_DYN alike). */
                        if (!have_tls ||
                            (state->work.sections[section].flags & SHF_TLS) == 0U ||
                            layout->work[section].vaddr < tls_base ||
                            source->value > UINT64_MAX -
                                (layout->work[section].vaddr - tls_base)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-final-tls-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                        output->st_value =
                            layout->work[section].vaddr - tls_base +
                            source->value;
                    } else if ((state->work.sections[section].flags &
                                SHF_ALLOC) != 0U) {
                        output->st_value =
                            layout->work[section].vaddr + source->value;
                    } else {
                        /* sh_addr is zero for non-ALLOC output sections.
                         * An input symbol must still include the fragment's
                         * displacement in its merged output section. */
                        const MiniLdOutputSection *owner =
                            &plan->sections[plan->work_shndx[section] - 1U];
                        uint64_t start = layout->work[section].vaddr;
                        if (start < owner->vaddr ||
                            source->value > UINT64_MAX -
                                (start - owner->vaddr)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-nonalloc-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                        output->st_value = start - owner->vaddr +
                                           source->value;
                    }
                }
            } else {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-final-symbol-section:%s\n",
                        source->name);
                return false;
            }
        }
        if (pass == 0U) {
            final->local_count = next;
        }
    }
    return next == final->entry_count;
}

/* ET_EXEC and unversioned ET_DYN retain their existing symbol behavior. */
static bool minild_final_symbols_build(const MiniLdState *state,
                                       const MiniLdLayout *layout,
                                       const MiniLdOutputPlan *plan,
                                       MiniLdFinalSymbols *final) {
    return minild_final_symbols_build_with_version(state, layout, plan,
                                                   NULL, final);
}

/* The final ELF section table and its synthetic symbol/name trailer have
 * one owner for ET_EXEC and ET_DYN.  Their LOAD segments and ELF headers
 * remain writer-specific; only physical ordering of the two symbol buffers
 * is a writer policy.  No canonical input or working-section state is
 * copied into this final-file projection. */
typedef enum MiniLdFinalSymbolTableOrder {
    MINILD_FINAL_SYMTAB_FIRST,
    MINILD_FINAL_STRTAB_FIRST
} MiniLdFinalSymbolTableOrder;

typedef struct MiniLdFinalSectionTables {
    MiniLdBuffer section_names;
    uint32_t *output_name_offsets;
    Elf64_Shdr *headers;
    size_t count;
    size_t symtab_index;
    size_t strtab_index;
    size_t shstrtab_index;
    size_t symtab_offset;
    size_t strtab_offset;
    size_t shstrtab_offset;
    size_t header_offset;
    size_t total_size;
} MiniLdFinalSectionTables;

static void minild_final_section_tables_destroy(
    MiniLdFinalSectionTables *tables) {
    free(tables->section_names.data);
    free(tables->output_name_offsets);
    free(tables->headers);
    memset(tables, 0, sizeof(*tables));
}

static bool minild_final_section_tables_build(
    const MiniLdOutputPlan *plan,
    const MiniLdFinalSymbols *symbols,
    size_t end_nonalloc,
    MiniLdFinalSymbolTableOrder order,
    MiniLdFinalSectionTables *tables) {
    size_t symbol_bytes;
    size_t cursor;
    size_t i;
    uint32_t symtab_name, strtab_name, shstrtab_name;

    memset(tables, 0, sizeof(*tables));
    if (plan->count > UINT16_MAX - 4U ||
        symbols->entry_count > SIZE_MAX / sizeof(Elf64_Sym)) {
        return false;
    }
    tables->count = plan->count + 4U;
    tables->symtab_index = plan->count + 1U;
    tables->strtab_index = plan->count + 2U;
    tables->shstrtab_index = plan->count + 3U;
    symbol_bytes = symbols->entry_count * sizeof(Elf64_Sym);
    tables->headers = calloc(tables->count, sizeof(*tables->headers));
    tables->output_name_offsets = calloc(plan->count == 0U ? 1U : plan->count,
                                          sizeof(*tables->output_name_offsets));
    if (tables->headers == NULL || tables->output_name_offsets == NULL ||
        !buffer_append_zero(&tables->section_names, 1U)) {
        return false;
    }
    for (i = 0U; i < plan->count; ++i) {
        if (!buffer_append_string(&tables->section_names,
                                  plan->sections[i].name,
                                  &tables->output_name_offsets[i])) {
            return false;
        }
    }
    if (!buffer_append_string(&tables->section_names, ".symtab",
                              &symtab_name) ||
        !buffer_append_string(&tables->section_names, ".strtab",
                              &strtab_name) ||
        !buffer_append_string(&tables->section_names, ".shstrtab",
                              &shstrtab_name)) {
        return false;
    }

    cursor = end_nonalloc;
    if (order == MINILD_FINAL_SYMTAB_FIRST) {
        if (!align_up_size(cursor, 8U, &tables->symtab_offset) ||
            !add_size(tables->symtab_offset, symbol_bytes,
                      &tables->strtab_offset) ||
            !add_size(tables->strtab_offset, symbols->names.size,
                      &tables->shstrtab_offset)) {
            return false;
        }
    } else {
        tables->strtab_offset = cursor;
        if (!add_size(cursor, symbols->names.size, &cursor) ||
            !align_up_size(cursor, 8U, &tables->symtab_offset) ||
            !add_size(tables->symtab_offset, symbol_bytes,
                      &tables->shstrtab_offset)) {
            return false;
        }
    }
    if (!add_size(tables->shstrtab_offset, tables->section_names.size,
                  &cursor) ||
        !align_up_size(cursor, 8U, &tables->header_offset) ||
        tables->count > (SIZE_MAX - tables->header_offset) /
                            sizeof(*tables->headers)) {
        return false;
    }
    tables->total_size = tables->header_offset +
                         tables->count * sizeof(*tables->headers);

    tables->headers[tables->symtab_index] = (Elf64_Shdr){
        .sh_name = symtab_name,
        .sh_type = SHT_SYMTAB,
        .sh_offset = tables->symtab_offset,
        .sh_size = symbol_bytes,
        .sh_link = (Elf64_Word)tables->strtab_index,
        .sh_info = (Elf64_Word)symbols->local_count,
        .sh_addralign = 8U,
        .sh_entsize = sizeof(Elf64_Sym)
    };
    tables->headers[tables->strtab_index] = (Elf64_Shdr){
        .sh_name = strtab_name,
        .sh_type = SHT_STRTAB,
        .sh_offset = tables->strtab_offset,
        .sh_size = symbols->names.size,
        .sh_addralign = 1U
    };
    tables->headers[tables->shstrtab_index] = (Elf64_Shdr){
        .sh_name = shstrtab_name,
        .sh_type = SHT_STRTAB,
        .sh_offset = tables->shstrtab_offset,
        .sh_size = tables->section_names.size,
        .sh_addralign = 1U
    };
    return true;
}

static void minild_final_section_tables_emit(
    const MiniLdFinalSectionTables *tables,
    const MiniLdFinalSymbols *symbols,
    unsigned char *image) {
    memcpy(image + tables->symtab_offset, symbols->entries,
           symbols->entry_count * sizeof(*symbols->entries));
    memcpy(image + tables->strtab_offset, symbols->names.data,
           symbols->names.size);
    memcpy(image + tables->shstrtab_offset, tables->section_names.data,
           tables->section_names.size);
    memcpy(image + tables->header_offset, tables->headers,
           tables->count * sizeof(*tables->headers));
}
