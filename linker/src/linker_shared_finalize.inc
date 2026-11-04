/* ET_DYN post-layout dynamic tables and relocation finalization.
 * __ehdr_start is finalized here after physical placement. */
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

static bool shared_fill_dynsym(MiniLdState *state,
                               const MiniLdLayout *layout,
                               const MiniLdOutputPlan *output_plan,
                               const MiniLdSharedImage *shared) {
    MiniLdSection *dynsym = &state->work.sections[shared->dynsym_section];
    size_t i;

    memset(dynsym->data, 0, dynsym->size);
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *input = &state->resolver.entries[i];
        size_t dynamic_index = shared->symbols[i].dynsym_index;
        Elf64_Sym output;

        if (dynamic_index == SIZE_MAX) {
            continue;
        }
        memset(&output, 0, sizeof(output));
        output.st_name = shared->symbols[i].dynstr_name_offset;
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
                if (!minild_output_plan_tls_symbol_value(
                        state, layout, output_plan, (size_t)input->section,
                        input->value, &output.st_value)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-shared-tls-symbol:%s\n",
                            input->name);
                    return false;
                }
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
        size_t dynamic_index = shared->symbols[i].dynsym_index;
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
        size_t index = shared->symbols[i].dynsym_index;
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
    MINILD_DYN(DT_SYMENT, shared->abi->dynsym_record_bytes);
    if (shared->rela_count != 0U) {
        MINILD_DYN(DT_RELA, layout->work[shared->rela_section].vaddr);
        MINILD_DYN(DT_RELASZ, state->work.sections[shared->rela_section].size);
        MINILD_DYN(DT_RELAENT, shared->abi->rela_record_bytes);
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

    if (index > SIZE_MAX / shared->abi->dynamic_record_bytes ||
        index * shared->abi->dynamic_record_bytes != dynamic->size) {
        fprintf(state->diagnostics,
                "minic-ld: shared-dynamic-size-mismatch\n");
        return false;
    }
    return true;
}
