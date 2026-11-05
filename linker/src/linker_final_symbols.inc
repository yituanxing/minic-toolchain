/* One final symbol-table owner for ET_EXEC and ET_DYN.  Input symbols and
 * their resolver indices are never reordered or annotated by the writer.
 * The final view owns the local-before-global order, SHN assignments,
 * relocated values and ELF string table; ET_REL keeps its source policy. */
typedef struct MiniLdFinalSymbols {
    MiniLdBuffer names;
    MiniElfSymbol *entries;
    size_t *resolver_to_final;
    size_t resolver_count;
    size_t entry_count;
    size_t local_count;
} MiniLdFinalSymbols;

static void minild_final_symbols_destroy(MiniLdFinalSymbols *final) {
    free(final->names.data);
    free(final->entries);
    free(final->resolver_to_final);
    memset(final, 0, sizeof(*final));
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
    /* GNU BFD finalizes script HIDDEN()/PROVIDE_HIDDEN() definitions as
     * local DEFAULT symbols in .symtab, while canonical resolution retains
     * their global identity and HIDDEN visibility until the final view. */
    if (source->source_symbol == SIZE_MAX &&
        source->section != MINILD_SECTION_UNDEF &&
        ELF64_ST_VISIBILITY(source->other) == STV_HIDDEN) {
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

/* A source ET_REL STT_SECTION is a relocation operand, not an independent
 * symbol in the linked image. Ordinary GNU final images omit it entirely;
 * Linux's AArch64 kernel-shared image instead emits one canonical symbol
 * per surviving output section for its section-relative dynamic records.
 * All source relocations have already been applied when --emit-relocs is
 * absent. Keep resolver identities untouched and project only this view.
 *
 * Likewise, local definitions from /DISCARD/ or --gc-sections cannot name
 * addresses in the final image. Preserving them as SHN_UNDEF creates tens of
 * thousands of fictitious local undefined symbols in real Linux Kbuild.
 * The existing --emit-relocs mode retains its source symbol projection until
 * final relocation addends can be rewritten to canonical section symbols. */
static bool minild_final_symbol_kept(const MiniLdState *state,
                                     const MiniLdOutputPlan *plan,
                                     const MiniLdResolvedSymbol *source,
                                     bool emit_relocs) {
    bool local = ELF64_ST_BIND(source->info) == STB_LOCAL;

    if (state->discard_locals && local &&
        source->name[0] == '.' && source->name[1] == 'L') {
        return false;
    }
    if (emit_relocs || !local) {
        return true;
    }
    if (ELF64_ST_TYPE(source->info) == STT_SECTION ||
        source->section == MINILD_SECTION_UNDEF) {
        return false;
    }
    return source->section < 0 ||
           ((size_t)source->section < plan->work_count &&
            plan->work_shndx[source->section] != 0U);
}

static bool minild_final_symbols_build_with_version(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *plan,
    const MiniLdScript *version_script,
    bool emit_relocs,
    MiniLdFinalSymbols *final) {
    size_t next = 1U;
    size_t pass, i, kept = 0U;
    /* Ordinary GNU ET_EXEC/ET_DYN without --emit-relocs does not require
     * STT_SECTION at all. The AArch64 Linux shared image is different:
     * its retained .rela.dyn section-symbol references use precisely one
     * canonical section symbol per output section. */
    size_t canonical_sections = !emit_relocs && state->kernel_shared_image
                                    ? plan->count : 0U;

    /* The final projection does not mutate source symbol identities used
     * by relocation and script evaluation. */
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *source = &state->resolver.entries[i];
        if (!minild_final_symbol_kept(state, plan, source, emit_relocs))
            continue;
        ++kept;
    }

    if (canonical_sections > UINT16_MAX ||
        kept > SIZE_MAX / sizeof(MiniElfSymbol) - 1U - canonical_sections ||
        state->resolver.count > SIZE_MAX / sizeof(size_t)) {
        return false;
    }
    final->entry_count = kept + 1U + canonical_sections;
    final->resolver_count = state->resolver.count;
    final->entries = calloc(final->entry_count,
                            sizeof(*final->entries));
    final->resolver_to_final = malloc((final->resolver_count == 0U ? 1U :
                                      final->resolver_count) *
                                     sizeof(*final->resolver_to_final));
    if (final->entries == NULL || final->resolver_to_final == NULL ||
        !buffer_append_zero(&final->names, 1U)) {
        return false;
    }
    for (i = 0U; i < final->resolver_count; ++i) {
        final->resolver_to_final[i] = SIZE_MAX;
    }
    for (i = 0U; i < canonical_sections; ++i) {
        MiniElfSymbol *output = &final->entries[next++];

        output->info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        output->other = STV_DEFAULT;
        output->section_index = (uint16_t)(i + 1U);
        output->value = plan->sections[i].vaddr;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < state->resolver.count; ++i) {
            const MiniLdResolvedSymbol *source = &state->resolver.entries[i];
            if (!minild_final_symbol_kept(state, plan, source, emit_relocs))
                continue;
            bool local = minild_final_symbol_local(source, state, plan,
                                                   version_script);
            bool localized = local &&
                             ELF64_ST_BIND(source->info) != STB_LOCAL;
            MiniElfSymbol *output;

            if (local != (pass == 0U)) {
                continue;
            }
            output = &final->entries[next++];
            final->resolver_to_final[i] = next - 1U;
            if (source->name[0] != '\0' &&
                !buffer_append_string(&final->names, source->name,
                                      &output->name)) {
                return false;
            }
            output->info = localized
                                  ? ELF64_ST_INFO(STB_LOCAL,
                                                  ELF64_ST_TYPE(source->info))
                                  : source->info;
            /* GNU's localized .symtab entries use DEFAULT visibility;
             * preserve input locals and target-specific high st_other bits. */
            output->other = localized
                                   ? (unsigned char)(source->other & ~3U)
                                   : source->other;
            output->size = source->size;
            if (source->section == MINILD_SECTION_UNDEF) {
                output->section_index = SHN_UNDEF;
            } else if (source->section == MINILD_SECTION_ABS) {
                output->section_index = SHN_ABS;
                output->value = source->value;
            } else if (source->section == MINILD_SECTION_COMMON) {
                output->section_index = SHN_COMMON;
                output->value = source->value;
            } else if (source->section >= 0 &&
                       (size_t)source->section < state->work.count) {
                size_t section = (size_t)source->section;
                if (plan->work_shndx[section] == 0U) {
                    output->section_index = SHN_UNDEF;
                } else {
                    output->section_index =
                        (uint16_t)plan->work_shndx[section];
                    if (ELF64_ST_TYPE(source->info) == STT_TLS) {
                        /* A final TLS symbol is relative to the TLS block,
                         * not the section VMA (ET_EXEC and ET_DYN alike). */
                        if (!minild_output_plan_tls_symbol_value(
                                state, layout, plan, section,
                                source->value, &output->value)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-final-tls-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                    } else if ((state->work.sections[section].flags &
                                SHF_ALLOC) != 0U) {
                        if (!minild_layout_section_symbol_value(
                                source, layout->work[section].vaddr,
                                &output->value)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-final-section-symbol:%s\n",
                                    source->name);
                            return false;
                        }
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
                        output->value = start - owner->vaddr +
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
                                       bool emit_relocs,
                                       MiniLdFinalSymbols *final) {
    return minild_final_symbols_build_with_version(state, layout, plan,
                                                   NULL, emit_relocs, final);
}

/* Final-link SHT_RELA data is indexed by final OutputSection, never by the
 * input section or an as-time work-fragment identity.  Restrict the first
 * x86 admission to untransformed relocations; GOT/IFUNC/TLS and relaxation
 * require a target-provided post-fixup relocation projection. */
typedef struct MiniLdFinalRelocSection {
    MiniElfRela *entries;
    size_t count;
    size_t capacity;
    size_t offset;
    uint32_t name_offset;
} MiniLdFinalRelocSection;

typedef struct MiniLdFinalRelocations {
    MiniLdFinalRelocSection *sections;
    size_t section_count;
    size_t active_count;
} MiniLdFinalRelocations;

static void minild_final_relocations_destroy(MiniLdFinalRelocations *final) {
    size_t i;

    for (i = 0U; i < final->section_count; ++i) {
        free(final->sections[i].entries);
    }
    free(final->sections);
    memset(final, 0, sizeof(*final));
}

static bool minild_final_relocations_build(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *plan, const MiniLdFinalSymbols *symbols,
    bool emit_relocs, MiniLdFinalRelocations *final) {
    size_t pass, i;

    memset(final, 0, sizeof(*final));
    if (!emit_relocs) {
        return true;
    }
    if (state->target == NULL || state->target->machine != EM_X86_64 ||
        state->target->elf_class != ELFCLASS64 ||
        plan->count > SIZE_MAX / sizeof(*final->sections)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-target-final-relocations\n");
        return false;
    }
    final->section_count = plan->count;
    final->sections = calloc(plan->count == 0U ? 1U : plan->count,
                             sizeof(*final->sections));
    if (final->sections == NULL) {
        return false;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        if (pass == 1U) {
            for (i = 0U; i < final->section_count; ++i) {
                MiniLdFinalRelocSection *out = &final->sections[i];

                if (out->capacity == 0U) {
                    continue;
                }
                out->entries = calloc(out->capacity, sizeof(*out->entries));
                if (out->entries == NULL) {
                    return false;
                }
                ++final->active_count;
            }
        }
        for (i = 0U; i < state->relocations.count; ++i) {
            const MiniLdReloc *source = &state->relocations.entries[i];
            const MiniLdSection *section;
            const MiniLdOutputSection *owner;
            MiniLdFinalRelocSection *out;
            size_t shndx, symbol_index;
            uint64_t place;

            if (source->section >= plan->work_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-final-relocation-source\n");
                return false;
            }
            shndx = minild_output_shndx(plan, source->section);
            if (shndx == 0U) {
                continue;
            }
            section = &state->work.sections[source->section];
            if (layout->scripted &&
                layout->work[source->section].discarded) {
                continue;
            }
            if (source->type == R_X86_64_NONE) {
                continue;
            }
            /* A scripted non-ALLOC section can carry a nonzero VMA (GNU
             * ld then emits absolute r_offset).  Our final non-ALLOC
             * sections deliberately have sh_addr=0 and section-relative
             * symbol values, so do not serialize a contradictory r_offset
             * until the final section-VMA policy can represent that case. */
            if ((section->flags & SHF_ALLOC) == 0U &&
                layout->work[source->section].vaddr != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-emit-relocs-nonalloc-vma\n");
                return false;
            }
            if (source->type != R_X86_64_64 &&
                source->type != R_X86_64_32 &&
                source->type != R_X86_64_32S &&
                source->type != R_X86_64_PC64 &&
                source->type != R_X86_64_PC32 &&
                source->type != R_X86_64_PLT32) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-emit-relocs-transform:%u\n",
                        (unsigned)source->type);
                return false;
            }
            if (source->symbol >= symbols->resolver_count ||
                (symbol_index = symbols->resolver_to_final[source->symbol]) ==
                    SIZE_MAX ||
                symbol_index > UINT32_MAX ||
                ELF64_ST_TYPE(state->resolver.entries[source->symbol].info) ==
                    STT_GNU_IFUNC) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-emit-relocs-symbol\n");
                return false;
            }
            owner = &plan->sections[shndx - 1U];
            if ((section->flags & SHF_ALLOC) != 0U) {
                if (layout->work[source->section].vaddr >
                    UINT64_MAX - source->offset ||
                    layout->work[source->section].vaddr < owner->vaddr) {
                    return false;
                }
                place = layout->work[source->section].vaddr +
                        source->offset;
                if (place < owner->vaddr ||
                    place - owner->vaddr >= owner->size) {
                    return false;
                }
            } else {
                size_t fragment_offset =
                    plan->work_file_offsets[source->section];
                if (fragment_offset < owner->file_offset ||
                    source->offset > UINT64_MAX -
                        (uint64_t)(fragment_offset - owner->file_offset)) {
                    return false;
                }
                place = (uint64_t)(fragment_offset - owner->file_offset) +
                        source->offset;
                if (place >= owner->size) {
                    return false;
                }
            }
            out = &final->sections[shndx - 1U];
            if (pass == 0U) {
                if (out->capacity == SIZE_MAX / sizeof(*out->entries)) {
                    return false;
                }
                ++out->capacity;
            } else {
                if (out->count >= out->capacity) {
                    return false;
                }
                out->entries[out->count++] = (MiniElfRela){
                    .offset = place,
                    .info = ELF64_R_INFO(symbol_index, source->type),
                    .addend = source->addend
                };
            }
        }
    }
    return true;
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
    MiniElfSection *headers;
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
    const MiniLdState *state,
    const MiniLdOutputPlan *plan,
    const MiniLdFinalSymbols *symbols,
    MiniLdFinalRelocations *relocations,
    size_t end_nonalloc,
    MiniLdFinalSymbolTableOrder order,
    MiniLdFinalSectionTables *tables) {
    size_t symbol_bytes;
    MiniLdElfAbiSizes abi;
    size_t cursor;
    size_t i;
    uint32_t symtab_name, strtab_name, shstrtab_name;

    memset(tables, 0, sizeof(*tables));
    if (state->target == NULL ||
        (state->target->elf_class != ELFCLASS32 &&
         state->target->elf_class != ELFCLASS64) ||
        plan->count > UINT16_MAX - 4U ||
        relocations->active_count > UINT16_MAX - 4U - plan->count) {
        return false;
    }
    abi = minild_elf_abi_sizes(state->target->elf_class);
    if (symbols->entry_count > SIZE_MAX / abi.sym ||
        symbols->local_count > UINT32_MAX) {
        return false;
    }
    tables->count = plan->count + relocations->active_count + 4U;
    tables->symtab_index = plan->count + relocations->active_count + 1U;
    tables->strtab_index = tables->symtab_index + 1U;
    tables->shstrtab_index = tables->symtab_index + 2U;
    symbol_bytes = symbols->entry_count * abi.sym;
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
    for (i = 0U; i < relocations->section_count; ++i) {
        MiniLdFinalRelocSection *reloc = &relocations->sections[i];
        size_t length;
        char *name;
        bool appended;

        if (reloc->capacity == 0U) {
            continue;
        }
        length = strlen(plan->sections[i].name);
        if (length > SIZE_MAX - 6U) {
            return false;
        }
        name = malloc(length + 6U);
        if (name == NULL) {
            return false;
        }
        memcpy(name, ".rela", 5U);
        memcpy(name + 5U, plan->sections[i].name, length + 1U);
        appended = buffer_append_string(&tables->section_names, name,
                                        &reloc->name_offset);
        free(name);
        if (!appended) {
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
    for (i = 0U; i < relocations->section_count; ++i) {
        MiniLdFinalRelocSection *reloc = &relocations->sections[i];

        if (reloc->capacity == 0U) {
            continue;
        }
        if (!align_up_size(cursor, abi.word_alignment, &reloc->offset) ||
            reloc->count > SIZE_MAX / sizeof(Elf64_Rela) ||
            !add_size(reloc->offset,
                      reloc->count * sizeof(Elf64_Rela), &cursor)) {
            return false;
        }
    }
    if (order == MINILD_FINAL_SYMTAB_FIRST) {
        if (!align_up_size(cursor, abi.word_alignment, &tables->symtab_offset) ||
            !add_size(tables->symtab_offset, symbol_bytes,
                      &tables->strtab_offset) ||
            !add_size(tables->strtab_offset, symbols->names.size,
                      &tables->shstrtab_offset)) {
            return false;
        }
    } else {
        tables->strtab_offset = cursor;
        if (!add_size(cursor, symbols->names.size, &cursor) ||
            !align_up_size(cursor, abi.word_alignment, &tables->symtab_offset) ||
            !add_size(tables->symtab_offset, symbol_bytes,
                      &tables->shstrtab_offset)) {
            return false;
        }
    }
    if (!add_size(tables->shstrtab_offset, tables->section_names.size,
                  &cursor) ||
        !align_up_size(cursor, abi.word_alignment, &tables->header_offset) ||
        tables->count > (SIZE_MAX - tables->header_offset) /
                            abi.shdr) {
        return false;
    }
    tables->total_size = tables->header_offset +
                         tables->count * abi.shdr;

    {
        size_t rel_shndx = plan->count + 1U;
        for (i = 0U; i < relocations->section_count; ++i) {
            const MiniLdFinalRelocSection *reloc = &relocations->sections[i];

            if (reloc->capacity == 0U) {
                continue;
            }
            tables->headers[rel_shndx++] = (MiniElfSection){
                .name = reloc->name_offset,
                .type = SHT_RELA,
                .flags = SHF_INFO_LINK,
                .offset = reloc->offset,
                .size = reloc->count * sizeof(Elf64_Rela),
                .link = (uint32_t)tables->symtab_index,
                .info = (uint32_t)(i + 1U),
                .alignment = abi.word_alignment,
                .entry_size = sizeof(Elf64_Rela)
            };
        }
    }

    tables->headers[tables->symtab_index] = (MiniElfSection){
        .name = symtab_name,
        .type = SHT_SYMTAB,
        .offset = tables->symtab_offset,
        .size = symbol_bytes,
        .link = (uint32_t)tables->strtab_index,
        .info = (uint32_t)symbols->local_count,
        .alignment = abi.word_alignment,
        .entry_size = abi.sym
    };
    tables->headers[tables->strtab_index] = (MiniElfSection){
        .name = strtab_name,
        .type = SHT_STRTAB,
        .offset = tables->strtab_offset,
        .size = symbols->names.size,
        .alignment = 1U
    };
    tables->headers[tables->shstrtab_index] = (MiniElfSection){
        .name = shstrtab_name,
        .type = SHT_STRTAB,
        .offset = tables->shstrtab_offset,
        .size = tables->section_names.size,
        .alignment = 1U
    };
    return true;
}

static void minild_final_section_tables_emit(
    const MiniLdFinalSectionTables *tables,
    const MiniLdFinalSymbols *symbols,
    unsigned char *image) {
    /* Final symbol and section headers are canonical descriptors; ELF32 or
     * ELF64 record packing is exclusively the final serializer's job. */
    memcpy(image + tables->strtab_offset, symbols->names.data,
           symbols->names.size);
    memcpy(image + tables->shstrtab_offset, tables->section_names.data,
           tables->section_names.size);
}
