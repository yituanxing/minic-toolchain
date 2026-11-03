/* One final symbol-table owner for ET_EXEC and ET_DYN.  Input symbols and
 * their resolver indices are never reordered or annotated by the writer.
 * The final view owns the local-before-global order, SHN assignments,
 * relocated values and ELF string table; ET_REL keeps its source policy. */
typedef struct MiniLdFinalSymbols {
    MiniLdBuffer names;
    Elf64_Sym *entries;
    size_t local_count;
} MiniLdFinalSymbols;

static void minild_final_symbols_destroy(MiniLdFinalSymbols *final) {
    free(final->names.data);
    free(final->entries);
    memset(final, 0, sizeof(*final));
}

static bool minild_final_tls_base_address(const MiniLdState *state,
                                          const MiniLdStaticLayout *layout,
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
        if (layout->section_vaddr[i] < base) {
            base = layout->section_vaddr[i];
        }
    }
    *present_out = base != UINT64_MAX;
    *base_out = base == UINT64_MAX ? 0U : base;
    return true;
}

static bool minild_final_symbols_build(const MiniLdState *state,
                                       const MiniLdStaticLayout *layout,
                                       const MiniLdOutputPlan *plan,
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
    final->entries = calloc(state->resolver.count + 1U,
                            sizeof(*final->entries));
    if (final->entries == NULL || !buffer_append_zero(&final->names, 1U)) {
        return false;
    }
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < state->resolver.count; ++i) {
            const MiniLdResolvedSymbol *source = &state->resolver.entries[i];
            bool local = ELF64_ST_BIND(source->info) == STB_LOCAL;
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
            output->st_info = source->info;
            output->st_other = source->other;
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
                            layout->section_vaddr[section] < tls_base ||
                            source->value > UINT64_MAX -
                                (layout->section_vaddr[section] - tls_base)) {
                            fprintf(state->diagnostics,
                                    "minic-ld: invalid-final-tls-symbol:%s\n",
                                    source->name);
                            return false;
                        }
                        output->st_value =
                            layout->section_vaddr[section] - tls_base +
                            source->value;
                    } else if ((state->work.sections[section].flags &
                                SHF_ALLOC) != 0U) {
                        output->st_value =
                            layout->section_vaddr[section] + source->value;
                    } else {
                        /* sh_addr is zero for non-ALLOC output sections.
                         * An input symbol must still include the fragment's
                         * displacement in its merged output section. */
                        const MiniLdOutputSection *owner =
                            &plan->sections[plan->work_shndx[section] - 1U];
                        uint64_t start = layout->section_vaddr[section];
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
    return next == state->resolver.count + 1U;
}
