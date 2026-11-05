/* Coalesce adjacent scripted PT_LOADs without moving sections or relocations.
 * A vmlinux linker script can place >10,000 individual input sections. */
typedef struct MiniLdStaticLoadSegment {
    uint64_t file_offset, vaddr, laddr, file_size, mem_size;
    uint32_t flags;
} MiniLdStaticLoadSegment;

static int static_load_segment_compare(const void *lhs, const void *rhs) {
    const MiniLdStaticLoadSegment *a = lhs, *b = rhs;

    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->file_offset != b->file_offset) {
        return a->file_offset < b->file_offset ? -1 : 1;
    }
    return 0;
}

/* Resolve a static PT_TLS file base from the actual final work-section
 * placement, including linker-script AT() addresses.  The ordinary RW LOAD
 * shortcut does not exist for scripted layouts.  Keep the required ELF
 * p_offset/p_vaddr congruence and reject interleaved non-TLS bytes. */
static bool minild_static_tls_file_location(
    const MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan, size_t *file_offset_out,
    uint64_t *physical_address_out) {
    const MiniLdTlsProjection *tls = &output_plan->tls;
    uint64_t base = tls->block_base;
    uint64_t end = tls->memory_end;
    size_t file_offset = 0U;
    uint64_t physical_base = base;
    bool found_file = false;
    bool found_physical = false;
    size_t i;

    if (!tls->valid_extents || !tls->has_nonempty_section ||
        tls->alignment == 0U ||
        end < base ||
        (tls->has_file_contents &&
         (tls->file_end < base || tls->file_end > end))) {
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        const MiniLdLayoutWorkPlacement *place = &layout->work[i];
        uint64_t start = place->vaddr;
        uint64_t finish;
        uint64_t delta;

        if (output_plan->work_shndx[i] == 0U ||
            (section->flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (section->size > UINT64_MAX - start) {
            return false;
        }
        finish = start + (uint64_t)section->size;
        if ((section->flags & SHF_TLS) == 0U) {
            /* The single PT_TLS memory image may include alignment holes,
             * but no unrelated RW fragment may occupy those bytes. */
            if ((section->flags & SHF_WRITE) != 0U &&
                section->size != 0U && start < end && finish > base) {
                return false;
            }
            continue;
        }
        if ((section->flags & SHF_WRITE) == 0U || start < base ||
            finish > end || place->laddr < start - base) {
            return false;
        }
        delta = start - base;
        if (!found_physical) {
            physical_base = place->laddr - delta;
            found_physical = true;
        } else if (physical_base != place->laddr - delta) {
            return false;
        }
        if (section->type == SHT_NOBITS || section->size == 0U) {
            continue;
        }
        if (output_plan->work_file_offsets[i] == SIZE_MAX ||
            delta > output_plan->work_file_offsets[i]) {
            return false;
        }
        if (!found_file) {
            file_offset = output_plan->work_file_offsets[i] - (size_t)delta;
            found_file = true;
        } else if (file_offset !=
                   output_plan->work_file_offsets[i] - (size_t)delta) {
            return false;
        }
    }
    if (!found_file) {
        if (!layout->scripted && layout->have_rw &&
            base >= layout->rw_vaddr &&
            base - layout->rw_vaddr <= SIZE_MAX &&
            layout->rw_file_offset <=
                SIZE_MAX - (size_t)(base - layout->rw_vaddr)) {
            file_offset = layout->rw_file_offset +
                          (size_t)(base - layout->rw_vaddr);
        } else {
            /* A pure NOBITS template has p_filesz=0.  No bytes must be
             * read at p_offset, but ELF still requires congruence. */
            file_offset = (size_t)(base % tls->alignment);
        }
    }
    if (tls->alignment > 1U &&
        (uint64_t)file_offset % tls->alignment != base % tls->alignment) {
        return false;
    }
    *file_offset_out = file_offset;
    *physical_address_out = physical_base;
    return true;
}

/* The static segment policy is deliberately distinct from ET_DYN: each
 * target chooses ordinary CRT header mapping while scripted Linux images
 * preserve LMA and merge only three-way-contiguous sections. */
static bool minild_static_program_plan_build(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    MiniLdProgramPlan *plan) {
    MiniLdStaticLoadSegment *loads = NULL;
    const MiniLdTlsProjection *tls = &output_plan->tls;
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    size_t count = 0U;
    size_t load_count;
    size_t tls_file_offset = 0U;
    uint64_t tls_paddr = 0U;
    size_t i;

    memset(plan, 0, sizeof(*plan));
    if (layout->scripted) {
        size_t scripted_count = 0U;
        loads = calloc(output_plan->count == 0U ? 1U : output_plan->count,
                       sizeof(*loads));
        if (loads == NULL) {
            goto oom;
        }
        for (i = 0U; i < output_plan->count; ++i) {
            const MiniLdOutputSection *section = &output_plan->sections[i];
            MiniLdStaticLoadSegment *load;

            if ((section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > UINT64_MAX - section->vaddr ||
                section->size > UINT64_MAX - section->laddr) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-load-range:%s\n",
                        section->name);
                goto failed;
            }
            load = &loads[scripted_count++];
            load->file_offset = section->file_offset;
            load->vaddr = section->vaddr;
            load->laddr = section->laddr;
            load->file_size = section->type == SHT_NOBITS ? 0U :
                              section->size;
            load->mem_size = section->size;
            load->flags = PF_R;
            if ((section->flags & SHF_WRITE) != 0U) {
                load->flags |= PF_W;
            }
            if ((section->flags & SHF_EXECINSTR) != 0U) {
                load->flags |= PF_X;
            }
        }
        qsort(loads, scripted_count, sizeof(*loads), static_load_segment_compare);
        for (i = 0U; i < scripted_count; ++i) {
            const MiniLdStaticLoadSegment *next = &loads[i];

            if (count != 0U) {
                MiniLdStaticLoadSegment *prev = &loads[count - 1U];
                uint64_t previous_vend, previous_lend, previous_fend;
                uint64_t vgap = 0U, lgap = 0U, fgap = 0U;
                uint32_t combined_flags = prev->flags | next->flags;
                uint64_t page_size = state->target->page_size;
                bool ordered, same_page, linear_gap, plain_contiguous;
                bool safe_permissions;

                if (prev->vaddr > UINT64_MAX - prev->mem_size ||
                    prev->laddr > UINT64_MAX - prev->mem_size ||
                    prev->file_offset > UINT64_MAX - prev->mem_size) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-load-range\n");
                    goto failed;
                }
                previous_vend = prev->vaddr + prev->mem_size;
                previous_lend = prev->laddr + prev->mem_size;
                /* A file-backed section after SHT_NOBITS must follow the
                 * entire memory span in the file, with zero bytes filling
                 * the former BSS tail.  Another NOBITS section can still
                 * extend that tail without increasing p_filesz. */
                previous_fend = prev->file_offset +
                    (next->file_size == 0U ? prev->file_size : prev->mem_size);
                same_page = page_size != 0U && prev->mem_size != 0U &&
                    next->mem_size != 0U &&
                    (previous_vend - 1U) / page_size ==
                    next->vaddr / page_size;
                ordered = next->vaddr >= previous_vend &&
                    next->laddr >= previous_lend &&
                    (next->file_size == 0U ||
                     next->file_offset >= previous_fend);
                if (ordered) {
                    vgap = next->vaddr - previous_vend;
                    lgap = next->laddr - previous_lend;
                    if (next->file_size != 0U) {
                        fgap = next->file_offset - previous_fend;
                    }
                }
                /* A NOBITS tail needs no corresponding file gap: the
                 * unwritten VMA alignment bytes are already zero-filled.
                 * A following PROGBITS section, in contrast, must have an
                 * exact file/VMA/LMA affine offset. */
                linear_gap = ordered &&
                    vgap == lgap &&
                    (next->file_size == 0U || vgap == fgap) &&
                    vgap <= UINT64_MAX - next->mem_size &&
                    prev->mem_size <= UINT64_MAX - vgap - next->mem_size &&
                    (next->file_size == 0U ||
                     (vgap <= UINT64_MAX - next->file_size &&
                      prev->mem_size <= UINT64_MAX - vgap - next->file_size));
                plain_contiguous = linear_gap && vgap == 0U &&
                    prev->file_size == prev->mem_size;
                /* Do not create an RWX load by merging an RX section with
                 * an independent RW section. If a member is already RWX,
                 * however, coalescing it with adjacent sections in the
                 * same mapped page cannot introduce a new W+X page.
                 * Linux .init.pi is explicitly WAX and may share its page
                 * with its preceding RW initialization metadata. */
                safe_permissions =
                    (combined_flags & (PF_W | PF_X)) != (PF_W | PF_X) ||
                    (prev->flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
                    (next->flags & (PF_W | PF_X)) == (PF_W | PF_X);

                /* A loader maps PT_LOAD at page granularity.  Separate RX
                 * text and R rodata mappings inside one page would let the
                 * later mapping remove execute permission from _start.
                 * Preserve distinct protection at page boundaries, but
                 * coalesce same-page spans only when file/VMA/LMA advance
                 * together (including zero-filled alignment holes).
                 * A BSS tail is padded with real zero file bytes only if a
                 * later file-backed section re-enters the same load page.
                 * Non-linear AT() mapping cannot share that load.
                 * GNU -n is a separate explicit non-page-mapped policy,
                 * which may intentionally coalesce writable text into RWE. */
                if ((state->nmagic && plain_contiguous) ||
                    (!state->nmagic &&
                     ((plain_contiguous && prev->flags == next->flags) ||
                      (same_page && linear_gap && safe_permissions)))) {
                    if (next->file_size != 0U) {
                        prev->file_size = prev->mem_size + vgap +
                                          next->file_size;
                    }
                    prev->mem_size += vgap + next->mem_size;
                    prev->flags = combined_flags;
                    continue;
                }
                if ((!state->nmagic && same_page) ||
                    next->vaddr < previous_vend) {
                    fprintf(state->diagnostics,
                            "minic-ld: unsafe-script-load-page-overlap:"
                            "vaddr=0x%llx:next=0x%llx:"
                            "file=0x%llx+0x%llx:next_file=0x%llx:"
                            "lma=0x%llx:next_lma=0x%llx:"
                            "flags=0x%x:next_flags=0x%x\n",
                            (unsigned long long)prev->vaddr,
                            (unsigned long long)next->vaddr,
                            (unsigned long long)prev->file_offset,
                            (unsigned long long)prev->file_size,
                            (unsigned long long)next->file_offset,
                            (unsigned long long)prev->laddr,
                            (unsigned long long)next->laddr,
                            prev->flags, next->flags);
                    goto failed;
                }
            }
            loads[count++] = *next;
        }
    } else {
        count = (layout->have_rx ? 1U : 0U) +
                (layout->have_rw ? 1U : 0U);
    }
    load_count = count;
    if (tls->has_nonempty_section) {
        uint64_t tls_base = tls->block_base;
        uint64_t tls_memsz;
        uint64_t tls_filesz;

        /* Static TLS is loaded by libc from PT_TLS.  Reject irregular
         * scripted / malformed placements instead of publishing offsets
         * that do not agree with the target's TPOFF relocations. */
        if (!minild_static_tls_file_location(
                state, layout, output_plan, &tls_file_offset, &tls_paddr)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-tls-segment-layout\n");
            goto failed;
        }
        tls_memsz = tls->memory_end - tls_base;
        tls_filesz = tls->has_file_contents
                         ? tls->file_end - tls_base : 0U;
        if (tls_filesz > tls_memsz) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-tls-segment-size\n");
            goto failed;
        }
        ++count;
    }
    if (abi.phdr == 0U || count > UINT16_MAX ||
        count > (SIZE_MAX - abi.ehdr) / abi.phdr) {
        fprintf(state->diagnostics, "minic-ld: too-many-static-segments\n");
        goto failed;
    }
    plan->entries = calloc(count == 0U ? 1U : count,
                           sizeof(*plan->entries));
    if (plan->entries == NULL) {
        goto oom;
    }
    plan->count = count;
    if (layout->scripted) {
        for (i = 0U; i < load_count; ++i) {
            const MiniLdStaticLoadSegment *load = &loads[i];
            MiniElfProgramHeader *ph = &plan->entries[i];
            if (!state->nmagic &&
                (state->target->page_size == 0U ||
                 load->file_offset % state->target->page_size !=
                     load->vaddr % state->target->page_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-load-page-congruence:"
                        "vaddr=0x%llx offset=0x%llx\n",
                        (unsigned long long)load->vaddr,
                        (unsigned long long)load->file_offset);
                goto failed;
            }
            ph->type = PT_LOAD;
            ph->flags = load->flags;
            ph->offset = load->file_offset;
            ph->virtual_address = load->vaddr;
            ph->physical_address = load->laddr;
            ph->file_size = load->file_size;
            ph->memory_size = load->mem_size;
            ph->alignment = state->nmagic
                                ? state->target->nmagic_load_alignment :
                            state->target->page_size;
        }
    } else {
        size_t index = 0U;
        if (layout->have_rx) {
            MiniElfProgramHeader *ph = &plan->entries[index++];
            if ((uint64_t)layout->rx_file_offset > layout->rx_vaddr ||
                layout->rx_file_size >
                    SIZE_MAX - layout->rx_file_offset) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-static-header-load-layout\n");
                goto failed;
            }
            ph->type = PT_LOAD;
            ph->flags = PF_R | PF_X;
            /* Keep the ELF/program headers mapped so __ehdr_start is
             * dereferenceable by real static libc. The first section keeps
             * its preexisting VMA and file offset. */
            if (state->target->map_static_elf_headers) {
                ph->offset = 0U;
                ph->virtual_address = ph->physical_address =
                    layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
                ph->file_size = ph->memory_size =
                    layout->rx_file_offset + layout->rx_file_size;
            } else {
                ph->offset = layout->rx_file_offset;
                ph->virtual_address = ph->physical_address = layout->rx_vaddr;
                ph->file_size = ph->memory_size = layout->rx_file_size;
            }
            ph->alignment = state->target->page_size;
        }
        if (layout->have_rw) {
            MiniElfProgramHeader *ph = &plan->entries[index++];
            ph->type = PT_LOAD;
            ph->flags = PF_R | PF_W;
            ph->offset = layout->rw_file_offset;
            ph->virtual_address = ph->physical_address = layout->rw_vaddr;
            ph->file_size = layout->rw_file_size;
            ph->memory_size = layout->rw_mem_size;
            ph->alignment = state->target->page_size;
        }
    }
    if (tls->has_nonempty_section) {
        MiniElfProgramHeader *ph = &plan->entries[load_count];

        ph->type = PT_TLS;
        ph->flags = PF_R;
        ph->offset = tls_file_offset;
        ph->virtual_address = tls->block_base;
        ph->physical_address = tls_paddr;
        ph->file_size = tls->has_file_contents
                            ? tls->file_end - tls->block_base : 0U;
        ph->memory_size = tls->memory_end - tls->block_base;
        ph->alignment = tls->alignment;
    }
    free(loads);
    return true;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:static-segments\n");
failed:
    free(loads);
    minild_program_plan_destroy(plan);
    return false;
}

/* A PHDRS directive owns the executable program headers.  In particular,
 * the script's explicit FLAGS and :phdr inheritance must survive the final
 * image projection; synthesizing coalesced PT_LOADs here would silently
 * discard the ABI contract of an architecture's vmlinux.lds. */
typedef struct MiniLdScriptSegmentExtent {
    uint64_t file_base, vaddr_base, laddr_base;
    uint64_t file_end, memory_end;
    uint64_t last_memory_end;
    uint32_t derived_flags;
    bool occupied, file_backed;
} MiniLdScriptSegmentExtent;

static bool minild_static_script_phdr_program_plan_build(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    MiniLdOutputPlan *output_plan,
    const MiniLdScript *script,
    MiniLdProgramPlan *plan) {
    const MiniLdTlsProjection *tls = &output_plan->tls;
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    MiniLdScriptSegmentExtent *extents = NULL;
    bool *inherited = NULL;
    size_t i, j, tls_file_offset = 0U;
    size_t load_count = script->phdr_count;
    uint64_t tls_paddr = 0U;
    bool ok = false;

    memset(plan, 0, sizeof(*plan));
    if (script->phdr_count == 0U || abi.phdr == 0U ||
        script->phdr_count > UINT16_MAX - (tls->has_nonempty_section ? 1U : 0U) ||
        script->phdr_count >
            (SIZE_MAX - abi.ehdr) / abi.phdr -
                (tls->has_nonempty_section ? 1U : 0U)) {
        fprintf(state->diagnostics, "minic-ld: invalid-static-script-PHDR-count\n");
        return false;
    }
    extents = calloc(load_count, sizeof(*extents));
    inherited = calloc(load_count, sizeof(*inherited));
    plan->count = load_count + (tls->has_nonempty_section ? 1U : 0U);
    plan->entries = calloc(plan->count, sizeof(*plan->entries));
    if (extents == NULL || inherited == NULL || plan->entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:script-PHDRS\n");
        goto done;
    }
    /* The parser accepts these for shared/vDSO scripts. A static image
     * cannot truthfully publish their semantics until the corresponding
     * dynamic and header-in-LOAD projections have been constructed. */
    for (j = 0U; j < load_count; ++j) {
        const MiniLdScriptPhdr *decl = &script->phdrs[j];
        MiniElfProgramHeader *ph = &plan->entries[j];
        if (decl->filehdr || decl->phdrs ||
            (strcmp(decl->type_name, "PT_LOAD") != 0 &&
             strcmp(decl->type_name, "PT_NOTE") != 0)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-static-script-PHDR-type-or-header:%s\n",
                    decl->name);
            goto done;
        }
        ph->type = strcmp(decl->type_name, "PT_LOAD") == 0
                       ? PT_LOAD : PT_NOTE;
        ph->flags = decl->have_flags ? decl->flags : PF_R;
        ph->alignment = ph->type == PT_LOAD
                            ? (state->nmagic
                                   ? state->target->nmagic_load_alignment
                                   : state->target->page_size)
                            : state->target->static_note_alignment;
    }
    /* GNU's initial default is the first PT_LOAD; each explicit :phdr
     * replaces the inherited association for subsequent alloc sections.
     * A section may explicitly belong to both LOAD and NOTE. */
    for (j = 0U; j < load_count; ++j) {
        if (plan->entries[j].type == PT_LOAD) {
            inherited[j] = true;
            break;
        }
    }
    if (j == load_count) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-static-script-PHDR-no-LOAD\n");
        goto done;
    }
    for (i = 0U; i < output_plan->count; ++i) {
        MiniLdOutputSection *section = &output_plan->sections[i];
        const MiniLdScriptOutputSection *script_section = NULL;
        uint32_t section_flags = PF_R;

        if (section->command != SIZE_MAX) {
            const MiniLdScriptCommand *command;
            if (section->command >= script->command_count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-static-script-section-command:%s\n",
                        section->name);
                goto done;
            }
            command = &script->commands[section->command];
            if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION) {
                script_section = &command->value.section;
            }
        }
        if (script_section != NULL && script_section->phdr_name_count != 0U) {
            memset(inherited, 0, load_count * sizeof(*inherited));
            for (j = 0U; j < script_section->phdr_name_count; ++j) {
                const char *name = script_section->phdr_names[j];
                size_t k;
                for (k = 0U; k < load_count; ++k) {
                    if (strcmp(script->phdrs[k].name, name) == 0) {
                        inherited[k] = true;
                        break;
                    }
                }
                if (k == load_count) {
                    fprintf(state->diagnostics,
                            "minic-ld: unknown-static-script-PHDR:%s\n", name);
                    goto done;
                }
            }
        }
        if ((section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
            continue;
        }
        if (section->size > UINT64_MAX - section->vaddr ||
            section->size > UINT64_MAX - section->laddr ||
            (section->type != SHT_NOBITS &&
             (section->file_offset == SIZE_MAX ||
              section->size > UINT64_MAX - section->file_offset))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-script-PHDR-section:%s\n",
                    section->name);
            goto done;
        }
        if ((section->flags & SHF_WRITE) != 0U) {
            section_flags |= PF_W;
        }
        if ((section->flags & SHF_EXECINSTR) != 0U) {
            section_flags |= PF_X;
        }
        for (j = 0U; j < load_count; ++j) {
            MiniLdScriptSegmentExtent *ext = &extents[j];
            const MiniElfProgramHeader *ph = &plan->entries[j];
            uint64_t delta, section_end = section->vaddr + section->size;

            if (!inherited[j]) {
                continue;
            }
            if (ph->type == PT_NOTE && section->type != SHT_NOTE) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-static-script-PT_NOTE-section:%s\n",
                        section->name);
                goto done;
            }
            if (!ext->occupied) {
                ext->occupied = true;
                ext->vaddr_base = section->vaddr;
                ext->laddr_base = section->laddr;
                ext->memory_end = section_end;
                ext->last_memory_end = section_end;
            } else {
                if (section->vaddr < ext->last_memory_end ||
                    section->laddr < ext->laddr_base ||
                    section->vaddr - ext->vaddr_base !=
                        section->laddr - ext->laddr_base) {
                    fprintf(state->diagnostics,
                            "minic-ld: nonaffine-static-script-PHDR-LMA:%s\n",
                            section->name);
                    goto done;
                }
                ext->memory_end = section_end;
                ext->last_memory_end = section_end;
            }
            ext->derived_flags |= section_flags;
            if (section->type == SHT_NOBITS) {
                continue;
            }
            delta = section->vaddr - ext->vaddr_base;
            if (!ext->file_backed) {
                /* NOBITS at the front still occupies virtual space. */
                if (section->file_offset < delta) {
                    fprintf(state->diagnostics,
                            "minic-ld: nonaffine-static-script-PHDR-file:%s\n",
                            section->name);
                    goto done;
                }
                ext->file_base = section->file_offset - delta;
                ext->file_backed = true;
            } else if (section->file_offset < ext->file_base ||
                       section->file_offset - ext->file_base != delta) {
                fprintf(state->diagnostics,
                        "minic-ld: nonaffine-static-script-PHDR-file:%s:"
                        "offset=0x%llx:base=0x%llx:"
                        "vaddr=0x%llx:vbase=0x%llx\n",
                        section->name,
                        (unsigned long long)section->file_offset,
                        (unsigned long long)ext->file_base,
                        (unsigned long long)section->vaddr,
                        (unsigned long long)ext->vaddr_base);
                goto done;
            }
            ext->file_end = section->file_offset + section->size;
        }
    }
    for (j = 0U; j < load_count; ++j) {
        const MiniLdScriptSegmentExtent *ext = &extents[j];
        const MiniLdScriptPhdr *decl = &script->phdrs[j];
        MiniElfProgramHeader *ph = &plan->entries[j];
        if (!ext->occupied) {
            /* Keep script declaration order, including empty PT_NOTE. */
            continue;
        }
        ph->virtual_address = ext->vaddr_base;
        ph->physical_address = ext->laddr_base;
        ph->offset = ext->file_backed ? ext->file_base : 0U;
        ph->file_size = ext->file_backed ? ext->file_end - ext->file_base : 0U;
        ph->memory_size = ext->memory_end - ext->vaddr_base;
        if (!decl->have_flags) {
            ph->flags = ext->derived_flags;
        }
        if (ph->type == PT_LOAD && ph->alignment > 1U &&
            ph->offset % ph->alignment !=
                ph->virtual_address % ph->alignment) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-script-PHDR-congruence:%s\n",
                    decl->name);
            goto done;
        }
    }
    /* No two output sections may claim the same file bytes after PHDR
     * normalization. Holes inside LOAD are zero bytes, not hidden aliases
     * of another section. */
    for (i = 0U; i < output_plan->count; ++i) {
        const MiniLdOutputSection *a = &output_plan->sections[i];
        if ((a->flags & SHF_ALLOC) == 0U || a->type == SHT_NOBITS ||
            a->size == 0U) {
            continue;
        }
        for (j = i + 1U; j < output_plan->count; ++j) {
            const MiniLdOutputSection *b = &output_plan->sections[j];
            if ((b->flags & SHF_ALLOC) != 0U &&
                b->type != SHT_NOBITS && b->size != 0U &&
                a->file_offset < b->file_offset + b->size &&
                b->file_offset < a->file_offset + a->size) {
                fprintf(state->diagnostics,
                        "minic-ld: overlapping-static-script-PHDR-files:%s:%s\n",
                        a->name, b->name);
                goto done;
            }
        }
    }
    if (tls->has_nonempty_section) {
        MiniElfProgramHeader *ph = &plan->entries[load_count];
        if (!minild_static_tls_file_location(state, layout, output_plan,
                                             &tls_file_offset, &tls_paddr)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-tls-segment-layout\n");
            goto done;
        }
        ph->type = PT_TLS;
        ph->flags = PF_R;
        ph->offset = tls_file_offset;
        ph->virtual_address = tls->block_base;
        ph->physical_address = tls_paddr;
        ph->file_size = tls->has_file_contents
                            ? tls->file_end - tls->block_base : 0U;
        ph->memory_size = tls->memory_end - tls->block_base;
        ph->alignment = tls->alignment;
    }
    ok = true;
done:
    free(extents);
    free(inherited);
    if (!ok) {
        minild_program_plan_destroy(plan);
    }
    return ok;
}

static bool static_write_executable(MiniLdState *state,
                                    const MiniLdLayout *layout,
                                    MiniLdOutputPlan *output_plan,
                                    const char *path,
                                    uint64_t entry,
                                    bool output_et_dyn,
                                    bool emit_relocs,
                                    const MiniLdScript *script) {
    MiniLdFinalImage final = {0};
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    size_t load_image_size;
    size_t i;
    bool ok = false;
    const uint16_t elf_type = output_et_dyn ? ET_DYN : ET_EXEC;

    if (!(script != NULL && script->phdr_count != 0U
              ? minild_static_script_phdr_program_plan_build(
                    state, layout, output_plan, script, &final.program)
              : minild_static_program_plan_build(
                    state, layout, output_plan, &final.program))) {
        goto done;
    }
    load_image_size = abi.ehdr + final.program.count * abi.phdr;

    if (layout->scripted) {
        for (i = 0U; i < output_plan->count; ++i) {
            const MiniLdOutputSection *section = &output_plan->sections[i];
            size_t end;

            if ((section->flags & SHF_ALLOC) == 0U ||
                section->type == SHT_NOBITS || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > SIZE_MAX ||
                !add_size(section->file_offset, (size_t)section->size, &end)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-section-file-offset:%s\n",
                        section->name);
                goto done;
            }
            if (end > load_image_size) {
                load_image_size = end;
            }
        }
    } else {
        if (layout->have_rx &&
            layout->rx_file_offset + layout->rx_file_size > load_image_size) {
            load_image_size = layout->rx_file_offset + layout->rx_file_size;
        }
        if (layout->have_rw &&
            layout->rw_file_offset + layout->rw_file_size > load_image_size) {
            load_image_size = layout->rw_file_offset + layout->rw_file_size;
        }
    }

    if (output_plan->count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-static-output-sections\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if (output_plan->work_shndx[i] != 0U &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->type != SHT_NOBITS && section->size != 0U &&
            (output_plan->work_file_offsets[i] == SIZE_MAX ||
             !range_ok(output_plan->work_file_offsets[i], section->size,
                       load_image_size))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-section-layout:%s\n",
                    section->name);
            goto done;
        }
    }
    if (!minild_final_image_prepare(state, layout, output_plan,
                                          &final, load_image_size, NULL,
                                          MINILD_FINAL_SYMTAB_FIRST,
                                          elf_type, emit_relocs)) {
        goto done;
    }
    ok = minild_final_write_image(state, layout, output_plan,
                                  &final.program, &final.tables,
                                  &final.symbols, &final.relocations,
                                  elf_type, entry, path);

done:
    /* Retain the prior fail-closed contract even if preparation failed
     * before the common ELF serializer was entered. */
    if (!ok) {
        (void)remove(path);
    }
    minild_final_image_destroy(&final);
    return ok;
}
