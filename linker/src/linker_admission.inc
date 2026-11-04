/* MiniLD ELF object/DSO admission owner. Archive selection and input
 * scheduling are included below only after the object ingestion routines.
 * Admission records immutable input identities; WorkSections are created
 * by the later post-admission projection. */

typedef struct MiniLdParsedGroup {
    size_t signature_symbol;
    uint32_t flags;
    bool selected;
} MiniLdParsedGroup;

static bool section_supported_for_input(const MiniElfSection *section) {
    if (section->type == SHT_SYMTAB_SHNDX ||
        section->type == SHT_REL) {
        return false;
    }
    return true;
}
static uint32_t input_group_u32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

/* Groups are selected before *any* member bytes or global definitions are
 * admitted.  In particular a discarded COMDAT must not introduce a duplicate
 * strong symbol or an undefined relocation into the link. */
static bool select_input_groups(MiniLdState *state,
                                const MiniElfView *elf,
                                size_t symtab_index,
                                size_t strtab_index,
                                size_t symbol_count,
                                const char *path,
                                MiniLdParsedGroup *groups,
                                size_t *group_for_section) {
    size_t i;

    for (i = 0U; i < elf->section_count; ++i) {
        group_for_section[i] = SIZE_MAX;
    }
    for (i = 1U; i < elf->section_count; ++i) {
        MiniElfSection group;
        const unsigned char *bytes;
        size_t length;
        MiniElfSymbol signature;
        const char *signature_name;
        uint32_t flags;
        size_t j;

        if (!minielf_section(elf, i, &group)) {
            return false;
        }
        if (group.type != SHT_GROUP) {
            continue;
        }
        if (group.link != symtab_index || group.info == 0U ||
            group.info >= symbol_count || group.size < 8U ||
            (group.size & 3U) != 0U ||
            !minielf_symbol(elf, symtab_index, group.info, &signature) ||
            !minielf_string(elf, strtab_index, signature.name,
                            &signature_name) ||
            !minielf_section_data(elf, i, &bytes, &length)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-group:%s:%zu\n", path, i);
            return false;
        }
        if (signature_name[0] == '\0' &&
            minielf_symbol_type(signature.info) == STT_SECTION &&
            signature.section_index < elf->section_count &&
            !minielf_section_name(elf, signature.section_index,
                                  &signature_name)) {
            return false;
        }
        flags = input_group_u32(bytes);
        if (signature_name[0] == '\0' ||
            (flags & ~(uint32_t)GRP_COMDAT) != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-section-group:%s:%zu\n", path, i);
            return false;
        }
        groups[i].signature_symbol = group.info;
        groups[i].flags = flags;
        groups[i].selected = true;
        if ((flags & GRP_COMDAT) != 0U) {
            if (!input_graph_select_comdat(&state->inputs,
                                           signature_name,
                                           &groups[i].selected)) {
                fprintf(state->diagnostics,
                        "minic-ld: out-of-memory:comdat-signature\n");
                return false;
            }
        }
        for (j = 4U; j < length; j += 4U) {
            size_t member = input_group_u32(bytes + j);
            MiniElfSection member_header;

            if (member == 0U || member >= elf->section_count ||
                group_for_section[member] != SIZE_MAX ||
                !minielf_section(elf, member, &member_header) ||
                member_header.type == SHT_GROUP ||
                (member_header.flags & SHF_GROUP) == 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-group-member:%s:%zu\n", path, member);
                return false;
            }
            group_for_section[member] = i;
        }
    }
    for (i = 1U; i < elf->section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(elf, i, &section)) {
            return false;
        }
        if ((section.flags & SHF_GROUP) != 0U &&
            group_for_section[i] == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: orphan-group-member:%s:%zu\n", path, i);
            return false;
        }
    }
    return true;
}

/* ELF processor-specific RISC-V attributes are a single 'A' format
 * marker followed by length-delimited vendor subsections. Ordinary
 * SHF_MERGE-style concatenation of complete input sections creates
 * 'A vendor A vendor', which readelf cannot parse. Preserve each input
 * vendor subsection (including unknown vendors) but emit 'A' only once.
 * Validate all bounds before accepting a foreign .o or archive member.
 * This is metadata, not executable data and not an ABI override. */
#define MINILD_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)
static bool riscv_attributes_valid(const unsigned char *bytes, size_t size) {
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

/* Admission records only immutable input identities and canonical symbol/RELA
 * references.  No WorkSection exists until the complete archive/COMDAT graph
 * has been admitted and the single post-admission projection runs. */
static bool process_input_data(MiniLdState *state,
                               const unsigned char *data,
                               size_t size,
                               const char *path) {
    MiniElfView elf;
    MiniElfSection shstrtab;
    MiniElfSection symtab;
    MiniElfSection strtab;
    size_t symtab_index = SIZE_MAX;
    size_t strtab_index = SIZE_MAX;
    size_t file_index = SIZE_MAX;
    MiniLdParsedGroup *groups = NULL;
    size_t *group_for_section = NULL;
    bool *link_order_targets = NULL;
    size_t *symbol_map = NULL;
    size_t symbol_count = 0U;
    size_t i;
    bool incoming_has_code = false;
    bool ok = false;

    if (!minielf_open(&elf, data, size) ||
        elf.elf_class != ELFCLASS64 ||
        elf.data_encoding != ELFDATA2LSB ||
        elf.type != ET_REL ||
        elf.machine != EM_RISCV ||
        elf.version != EV_CURRENT ||
        elf.section_count == 0U ||
        elf.section_name_table_index == SHN_UNDEF) {
        fprintf(state->diagnostics, "minic-ld: unsupported-elf:%s\n", path);
        goto done;
    }
    if (!minielf_section(&elf, elf.section_name_table_index, &shstrtab) ||
        shstrtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics, "minic-ld: invalid-shstrtab:%s\n", path);
        goto done;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (section.type == SHT_PROGBITS && section.size != 0U &&
            (section.flags & (SHF_ALLOC | SHF_EXECINSTR)) ==
                (SHF_ALLOC | SHF_EXECINSTR)) {
            incoming_has_code = true;
        }
        if (section.type == SHT_SYMTAB) {
            if (symtab_index != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: multiple-symtabs:%s\n", path);
                goto done;
            }
            symtab = section;
            symtab_index = i;
        }
    }
    if (symtab_index == SIZE_MAX ||
        symtab.link >= elf.section_count ||
        symtab.entry_size < sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics, "minic-ld: missing-symtab:%s\n", path);
        goto done;
    }
    strtab_index = symtab.link;
    if (!minielf_section(&elf, strtab_index, &strtab) ||
        strtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics, "minic-ld: invalid-symtab:%s\n", path);
        goto done;
    }
    symbol_count = minielf_symbol_count(&elf, &symtab);
    if (symbol_count == 0U) {
        fprintf(state->diagnostics, "minic-ld: invalid-symtab:%s\n", path);
        goto done;
    }
    groups = calloc(elf.section_count, sizeof(*groups));
    group_for_section = malloc(elf.section_count * sizeof(*group_for_section));
    link_order_targets = calloc(elf.section_count, sizeof(*link_order_targets));
    if (groups == NULL || group_for_section == NULL ||
        link_order_targets == NULL ||
        !select_input_groups(state, &elf, symtab_index, strtab_index,
                             symbol_count, path, groups, group_for_section)) {
        if (groups == NULL || group_for_section == NULL ||
            link_order_targets == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:input-groups\n");
        }
        goto done;
    }
    /* A relocatable LINK_ORDER target needs its own source identity when
     * the post-admission projection chooses distinct group/target owners. */
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection header;

        if (!minielf_section(&elf, i, &header)) {
            goto done;
        }
        if ((header.flags & SHF_LINK_ORDER) != 0U && header.link != 0U) {
            if (header.link >= elf.section_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-link-order:%s:%zu\n", path, i);
                goto done;
            }
            link_order_targets[header.link] = true;
        }
    }
    if (!input_graph_record_file(&state->inputs, path, elf.flags,
                                 elf.section_count, &file_index)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:input-file\n");
        goto done;
    }

    /* Selected group headers are source-only nodes. Their original
     * flags, signature's ELF symbol index and member relationships are
     * sufficient to reconstruct -r group output after admission. */
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection header;
        const char *name;
        const unsigned char *bytes;
        size_t length;
        size_t source_id;

        if (!minielf_section(&elf, i, &header) ||
            header.type != SHT_GROUP || !groups[i].selected) {
            continue;
        }
        if (!minielf_section_name(&elf, i, &name) ||
            !minielf_section_data(&elf, i, &bytes, &length) ||
            !input_graph_record_section(&state->inputs, file_index, i,
                                        name, &header, bytes, length,
                                        &source_id)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-input-group-header:%s:%zu\n", path, i);
            goto done;
        }
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection input;
        const char *name;
        const unsigned char *section_data;
        size_t section_size;
        size_t group_index;
        size_t source_id;

        if (!minielf_section(&elf, i, &input)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (i == symtab_index || i == strtab_index ||
            i == elf.section_name_table_index ||
            input.type == SHT_RELA || input.type == SHT_GROUP) {
            continue;
        }
        group_index = group_for_section[i];
        if (group_index != SIZE_MAX && !groups[group_index].selected) {
            continue;
        }
        if (!section_supported_for_input(&input)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-section-type:%s:index=%zu:type=%u\n",
                    path, i, input.type);
            goto done;
        }
        if (!minielf_section_name(&elf, i, &name)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-name:%s\n", path);
            goto done;
        }
        if (input.type == MINILD_SHT_RISCV_ATTRIBUTES) {
            if (strcmp(name, ".riscv.attributes") != 0 ||
                input.flags != 0U ||
                !minielf_section_data(&elf, i, &section_data,
                                      &section_size) ||
                !riscv_attributes_valid(section_data, section_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-attributes:%s\n", path);
                goto done;
            }
        } else if (input.size > SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: section-too-large:%s\n", name);
            goto done;
        } else if (input.type == SHT_NOBITS) {
            section_data = NULL;
            section_size = 0U;
        } else if (!minielf_section_data(&elf, i,
                                          &section_data, &section_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-data:%s\n", name);
            goto done;
        }
        if (!input_graph_record_section(
                &state->inputs, file_index, i, name, &input,
                input.type == SHT_NOBITS ? NULL : section_data,
                input.type == SHT_NOBITS ? 0U : section_size,
                &source_id)) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:input-section\n");
            goto done;
        }
        state->inputs.sections[source_id].link_order_target =
            link_order_targets[i];
        if (group_index != SIZE_MAX) {
            size_t group_id = input_graph_section_id(
                &state->inputs, file_index, group_index);
            if (group_id == SIZE_MAX ||
                group_id >= state->inputs.section_count ||
                state->inputs.sections[group_id].type != SHT_GROUP) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-input-group-owner:%s:%zu\n",
                        path, i);
                goto done;
            }
            state->inputs.sections[source_id].group_input = group_id;
        }
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection input;
        size_t source_id = input_graph_section_id(
            &state->inputs, file_index, i);
        MiniLdInputSection *source;
        size_t target_id;

        if (source_id == SIZE_MAX ||
            state->inputs.sections[source_id].type == SHT_GROUP ||
            !minielf_section(&elf, i, &input) ||
            (input.flags & SHF_LINK_ORDER) == 0U || input.link == 0U) {
            continue;
        }
        if (input.link >= elf.section_count ||
            (target_id = input_graph_section_id(
                 &state->inputs, file_index, input.link)) == SIZE_MAX ||
            state->inputs.sections[target_id].type == SHT_GROUP) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-link-order:%s:%zu\n", path, i);
            goto done;
        }
        source = &state->inputs.sections[source_id];
        source->link_order_input = target_id;
    }

    symbol_map = malloc(symbol_count * sizeof(*symbol_map));
    if (symbol_map == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:symbol-map\n");
        goto done;
    }
    for (i = 0U; i < symbol_count; ++i) {
        symbol_map[i] = SIZE_MAX;
    }

    for (i = 1U; i < symbol_count; ++i) {
        MiniElfSymbol input_symbol;
        const char *name;
        int source_section = MINILD_SECTION_UNDEF;
        uint64_t value;
        size_t input_section = SIZE_MAX;
        size_t source_symbol;
        bool group_signature_section = false;
        size_t canonical_symbol;
        unsigned bind;

        if (!minielf_symbol(&elf, symtab_index, i, &input_symbol)) {
            fprintf(state->diagnostics,
                    "minic-ld: truncated-symbol:%s\n", path);
            goto done;
        }
        if (!minielf_string(&elf, strtab_index, input_symbol.name, &name)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-symbol-name:%s\n", path);
            goto done;
        }
        /* Even a discarded COMDAT local retains its original candidate
         * source record; only its canonical resolution link is absent. */
        if (!input_graph_record_symbol(&state->inputs, state->diagnostics,
                                       file_index, i, name, &input_symbol,
                                       &source_symbol)) {
            goto done;
        }
        value = input_symbol.value;
        if (input_symbol.section_index == SHN_UNDEF) {
            source_section = MINILD_SECTION_UNDEF;
        } else if (input_symbol.section_index == SHN_ABS) {
            source_section = MINILD_SECTION_ABS;
        } else if (input_symbol.section_index == SHN_COMMON) {
            source_section = MINILD_SECTION_COMMON;
        } else if (input_symbol.section_index < elf.section_count &&
                   groups[input_symbol.section_index].signature_symbol == i &&
                   minielf_symbol_type(input_symbol.info) == STT_SECTION) {
            source_section = MINILD_SECTION_ABS;
            group_signature_section = true;
        } else if (input_symbol.section_index < elf.section_count &&
                   group_for_section[input_symbol.section_index] != SIZE_MAX &&
                   !groups[group_for_section[
                       input_symbol.section_index]].selected) {
            source_section = MINILD_SECTION_UNDEF;
            if (minielf_symbol_bind(input_symbol.info) == STB_LOCAL) {
                continue;
            }
        } else if (input_symbol.section_index >= elf.section_count ||
                   (input_section = input_graph_section_id(
                        &state->inputs, file_index,
                        input_symbol.section_index)) == SIZE_MAX ||
                   state->inputs.sections[input_section].type == SHT_GROUP) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-symbol-section:%s:%s\n",
                    path, name);
            goto done;
        } else {
            /* Archive and weak/strong resolution need 'defined' but no
             * work-section index exists until final input projection. */
            source_section = MINILD_SECTION_PENDING;
        }
        state->inputs.symbols[source_symbol].input_section = input_section;

        bind = minielf_symbol_bind(input_symbol.info);
        if (bind == STB_LOCAL) {
            if (!symbol_resolver_add_local(
                    &state->resolver, state->diagnostics, name,
                    source_section, value, input_symbol.size,
                    input_symbol.info, input_symbol.other, source_symbol,
                    &canonical_symbol)) {
                goto done;
            }
        } else if (bind == STB_GLOBAL || bind == STB_WEAK) {
            if (!symbol_resolver_merge_global(
                    &state->resolver, state->diagnostics, name,
                    source_section, value, input_symbol.size,
                    input_symbol.info, input_symbol.other, source_symbol,
                    &canonical_symbol)) {
                goto done;
            }
        } else {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-symbol-binding:%s:%s:%u\n",
                    path, name, bind);
            goto done;
        }
        symbol_map[i] = canonical_symbol;
        state->inputs.symbols[source_symbol].canonical_symbol =
            canonical_symbol;
        if (group_signature_section) {
            state->resolver.entries[canonical_symbol]
                .group_signature_section = true;
        }
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection rela_section;
        size_t count;
        size_t j;
        size_t target_input_id;

        if (!minielf_section(&elf, i, &rela_section)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-section-table:%s\n", path);
            goto done;
        }
        if (rela_section.type != SHT_RELA) {
            continue;
        }
        if (group_for_section[i] != SIZE_MAX &&
            !groups[group_for_section[i]].selected) {
            continue;
        }
        if (rela_section.link != symtab_index ||
            rela_section.info >= elf.section_count ||
            (target_input_id = input_graph_section_id(
                 &state->inputs, file_index, rela_section.info)) ==
                SIZE_MAX ||
            state->inputs.sections[target_input_id].type == SHT_GROUP ||
            rela_section.entry_size < sizeof(Elf64_Rela)) {
            fprintf(state->diagnostics, "minic-ld: invalid-rela:%s\n", path);
            goto done;
        }
        count = minielf_rela_count(&elf, &rela_section);
        if (count == 0U && rela_section.size != 0U) {
            fprintf(state->diagnostics, "minic-ld: invalid-rela:%s\n", path);
            goto done;
        }
        for (j = 0U; j < count; ++j) {
            MiniElfRela rela;
            size_t input_symbol_index;
            size_t canonical_symbol = SIZE_MAX;

            if (!minielf_rela(&elf, i, j, &rela)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-rela:%s\n", path);
                goto done;
            }
            input_symbol_index = minielf_rela_symbol(&elf, rela.info);
            if (input_symbol_index >= symbol_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-rela-symbol:%s\n", path);
                goto done;
            }
            if (input_symbol_index != 0U) {
                canonical_symbol = symbol_map[input_symbol_index];
                if (canonical_symbol == SIZE_MAX) {
                    fprintf(state->diagnostics,
                            "minic-ld: unmapped-rela-symbol:%s\n", path);
                    goto done;
                }
            }
            if (!relocations_append(
                    &state->relocations, state->diagnostics,
                    SIZE_MAX, rela.offset,
                    minielf_rela_type(&elf, rela.info), canonical_symbol,
                    rela.addend, target_input_id, rela.offset)) {
                goto done;
            }
        }
    }

    {
        uint32_t merged_flags = 0U;
        const char *abi_reason = NULL;

        if (!minild_riscv_elf_abi_merge(
                state->elf_flags, elf.flags,
                state->processed_object_count != 0U, incoming_has_code,
                &merged_flags, &abi_reason)) {
            fprintf(state->diagnostics,
                    "minic-ld: incompatible-riscv-elf-flags:%s:%s:"
                    "current=0x%08x:incoming=0x%08x\n",
                    path,
                    abi_reason == NULL ? "unknown" : abi_reason,
                    (unsigned)state->elf_flags, (unsigned)elf.flags);
            goto done;
        }
        state->elf_flags = merged_flags;
    }
    ++state->processed_object_count;
    state->have_input = true;
    ok = true;

done:
    free(symbol_map);
    free(group_for_section);
    free(link_order_targets);
    free(groups);
    return ok;
}

static bool process_dso_input_data(MiniLdState *state,
                                   const unsigned char *data,
                                   size_t size,
                                   const char *path) {
    MiniElfView elf;
    MiniElfSection dynsym = {0};
    MiniElfSection dynamic = {0};
    size_t dynsym_index = SIZE_MAX;
    size_t dynamic_index = SIZE_MAX;
    const char *needed_name = NULL;
    size_t i;

    if (!minielf_open(&elf, data, size) ||
        elf.elf_class != ELFCLASS64 ||
        elf.data_encoding != ELFDATA2LSB ||
        elf.type != ET_DYN ||
        elf.machine != EM_RISCV ||
        elf.version != EV_CURRENT) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-input:%s\n",
                path);
        return false;
    }
    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-input-section:%s\n",
                    path);
            return false;
        }
        if (section.type == SHT_DYNSYM) {
            if (dynsym_index != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: multiple-dynsym:%s\n",
                        path);
                return false;
            }
            dynsym = section;
            dynsym_index = i;
        } else if (section.type == SHT_DYNAMIC &&
                   dynamic_index == SIZE_MAX) {
            dynamic = section;
            dynamic_index = i;
        }
    }
    if (dynsym_index == SIZE_MAX ||
        dynsym.link >= elf.section_count ||
        dynsym.entry_size < sizeof(Elf64_Sym)) {
        fprintf(state->diagnostics,
                "minic-ld: missing-dynsym:%s\n",
                path);
        return false;
    }

    if (dynamic_index != SIZE_MAX) {
        const unsigned char *dynamic_data;
        size_t dynamic_size;
        size_t entry_size =
            dynamic.entry_size == 0U ? sizeof(Elf64_Dyn)
                                     : (size_t)dynamic.entry_size;

        if (dynamic.link >= elf.section_count ||
            entry_size < sizeof(Elf64_Dyn) ||
            !minielf_section_data(&elf,
                                  dynamic_index,
                                  &dynamic_data,
                                  &dynamic_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-dynamic:%s\n",
                    path);
            return false;
        }
        for (i = 0U; i + sizeof(Elf64_Dyn) <= dynamic_size;
             i += entry_size) {
            Elf64_Dyn entry;

            if (entry_size > dynamic_size - i) {
                break;
            }
            memcpy(&entry, dynamic_data + i, sizeof(entry));
            if (entry.d_tag == DT_NULL) {
                break;
            }
            if (entry.d_tag == DT_SONAME) {
                if (entry.d_un.d_val > UINT32_MAX ||
                    !minielf_string(&elf,
                                    dynamic.link,
                                    (uint32_t)entry.d_un.d_val,
                                    &needed_name)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-shared-soname:%s\n",
                            path);
                    return false;
                }
                break;
            }
        }
    }
    if (needed_name == NULL || needed_name[0] == '\0') {
        const char *slash = strrchr(path, '/');
        needed_name = slash == NULL ? path : slash + 1;
    }
    if (needed_name[0] == '\0' ||
        !state_add_dso_needed(state, needed_name)) {
        return false;
    }

    {
        size_t count = minielf_symbol_count(&elf, &dynsym);

        if (count == 0U && dynsym.size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-dynsym:%s\n",
                    path);
            return false;
        }
        for (i = 1U; i < count; ++i) {
            MiniElfSymbol symbol;
            const char *name;
            unsigned bind;
            unsigned visibility;

            if (!minielf_symbol(&elf, dynsym_index, i, &symbol)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-dynsym:%s\n",
                        path);
                return false;
            }
            bind = minielf_symbol_bind(symbol.info);
            visibility = ELF64_ST_VISIBILITY(symbol.other);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                symbol.section_index == SHN_UNDEF ||
                symbol.name == 0U ||
                (visibility != STV_DEFAULT &&
                 visibility != STV_PROTECTED)) {
                continue;
            }
            if (!minielf_string(&elf, dynsym.link, symbol.name, &name)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-dynsym-name:%s\n",
                        path);
                return false;
            }
            if (!state_add_dso_symbol(state, name)) {
                return false;
            }
        }
    }
    return true;
}

static bool process_input(MiniLdState *state, const char *path) {
    unsigned char *data = NULL;
    size_t size = 0U;
    MiniElfView elf;
    bool ok;

    if (!read_file(path, &data, &size, state->diagnostics)) {
        return false;
    }
    if (minielf_open(&elf, data, size) && elf.type == ET_DYN) {
        if (!state->allow_dso_inputs) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-input-requires-dynamic-link:%s\n",
                    path);
            ok = false;
        } else {
            ok = process_dso_input_data(state, data, size, path);
        }
    } else {
        ok = process_input_data(state, data, size, path);
    }
    free(data);
    return ok;
}

#include "linker_archive_admission.inc"
#include "linker_input_sequence.inc"
