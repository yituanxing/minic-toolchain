/* A working section is the unit of relocation and relaxation, but a linker
 * script output section owns an ordered collection of those fragments. The
 * ELF section table must describe the latter. Keep this projection separate
 * from the working sections so symbol and LINK_ORDER references can share a
 * single, stable work-section -> final-shndx mapping. */
typedef struct MiniLdOutputSection {
    const char *name;
    size_t command;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    uint64_t vaddr;
    uint64_t laddr;
    uint64_t size;
    size_t file_offset;
    size_t *members;
    size_t member_count;
    size_t member_capacity;
    unsigned char fill;
    bool have_fill;
    bool have_content;
} MiniLdOutputSection;

typedef struct MiniLdOutputPlan {
    MiniLdOutputSection *sections;
    size_t count;
    /* Fixed at projection time: the plan, not MiniLdState, owns map bounds. */
    size_t work_count;
    /* Zero means discarded, otherwise final ELF section header index. */
    size_t *work_shndx;
    /* Final physical offsets of surviving work fragments, including
     * non-ALLOC members that acquire file storage after LOAD layout. */
    size_t *work_file_offsets;
    size_t *command_shndx;
} MiniLdOutputPlan;

static void minild_output_plan_destroy(MiniLdOutputPlan *plan) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        free(plan->sections[i].members);
    }
    free(plan->sections);
    free(plan->work_shndx);
    free(plan->work_file_offsets);
    free(plan->command_shndx);
    memset(plan, 0, sizeof(*plan));
}

static size_t minild_output_shndx(const MiniLdOutputPlan *plan,
                                  size_t work) {
    return work < plan->work_count ? plan->work_shndx[work] : 0U;
}

static int minild_output_section_compare(const void *left,
                                         const void *right) {
    const MiniLdOutputSection *a = left;
    const MiniLdOutputSection *b = right;
    if (a->command != b->command) {
        return a->command < b->command ? -1 : 1;
    }
    return a->members[0] < b->members[0] ? -1 :
           a->members[0] > b->members[0] ? 1 : 0;
}

static bool minild_output_plan_build(const MiniLdState *state,
                                     const MiniLdLayout *layout,
                                     MiniLdOutputPlan *plan) {
    size_t *owner_to_output = NULL;
    size_t i;
    bool ok = false;

    memset(plan, 0, sizeof(*plan));
    plan->work_count = state->work.count;
    plan->sections = calloc(state->work.count == 0U ? 1U :
                            state->work.count, sizeof(*plan->sections));
    plan->work_shndx = calloc(state->work.count == 0U ? 1U :
                              state->work.count, sizeof(*plan->work_shndx));
    plan->work_file_offsets = calloc(state->work.count == 0U ? 1U :
                                     state->work.count,
                                     sizeof(*plan->work_file_offsets));
    if (plan->sections == NULL || plan->work_shndx == NULL ||
        plan->work_file_offsets == NULL) {
        goto done;
    }
    for (i = 0U; i < state->work.count; ++i) {
        plan->work_file_offsets[i] = layout->work[i].file_offset;
    }
    if (layout->scripted) {
        size_t count = layout->script_command_count;
        if (count > SIZE_MAX / sizeof(*owner_to_output)) {
            goto done;
        }
        owner_to_output = malloc((count == 0U ? 1U : count) *
                                 sizeof(*owner_to_output));
        plan->command_shndx = calloc(count == 0U ? 1U : count,
                                    sizeof(*plan->command_shndx));
        if (owner_to_output == NULL || plan->command_shndx == NULL) {
            goto done;
        }
        for (i = 0U; i < count; ++i) {
            owner_to_output[i] = SIZE_MAX;
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *part = &state->work.sections[i];
        size_t command = layout->scripted && layout->work != NULL
                             ? layout->work[i].owner_command : SIZE_MAX;
        MiniLdOutputSection *output = NULL;

        if (part->gc_discarded ||
            (layout->scripted && layout->work[i].discarded)) {
            continue;
        }
        if (command != SIZE_MAX && command < layout->script_command_count &&
            owner_to_output[command] != SIZE_MAX) {
            output = &plan->sections[owner_to_output[command]];
        }
        if (output == NULL) {
            output = &plan->sections[plan->count++];
            output->name = part->output_name != NULL
                               ? part->output_name : part->name;
            output->command = command;
            output->file_offset = SIZE_MAX;
            if (command != SIZE_MAX && command < layout->script_command_count) {
                owner_to_output[command] = plan->count - 1U;
            }
            if (command != SIZE_MAX && layout->script_outputs != NULL &&
                layout->script_outputs[command].have_fill) {
                output->have_fill = true;
                output->fill = layout->script_outputs[command].fill;
            }
        }
        if (output->member_count == output->member_capacity) {
            size_t capacity = output->member_capacity == 0U
                                  ? 4U : output->member_capacity * 2U;
            size_t *members;
            if (capacity < output->member_capacity ||
                capacity > SIZE_MAX / sizeof(*members)) {
                goto done;
            }
            members = realloc(output->members, capacity * sizeof(*members));
            if (members == NULL) {
                goto done;
            }
            output->members = members;
            output->member_capacity = capacity;
        }
        output->members[output->member_count++] = i;
        if (part->align > output->align) {
            output->align = part->align;
        }
        if (part->size == 0U && output->have_content) {
            continue;
        }
        if (!output->have_content || (output->size == 0U && part->size != 0U)) {
            output->type = part->type;
            output->flags = part->flags;
            output->entsize = part->entsize;
            output->vaddr = layout->work[i].vaddr;
            output->laddr = layout->work[i].laddr;
            output->size = part->size;
            output->file_offset = layout->work[i].file_offset;
            output->have_content = true;
        } else if (part->size != 0U) {
            uint64_t low = output->vaddr;
            uint64_t high;
            uint64_t end;
            if (part->type != output->type || part->flags != output->flags ||
                part->entsize != output->entsize ||
                part->size > UINT64_MAX - layout->work[i].vaddr ||
                output->size > UINT64_MAX - output->vaddr) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-type:%s\n",
                        output->name);
                goto done;
            }
            high = output->vaddr + output->size;
            end = layout->work[i].vaddr + part->size;
            if (layout->work[i].vaddr < low) {
                low = layout->work[i].vaddr;
                output->laddr = layout->work[i].laddr;
            }
            if (end > high) {
                high = end;
            }
            output->vaddr = low;
            output->size = high - low;
            if (part->type != SHT_NOBITS &&
                layout->work[i].file_offset < output->file_offset) {
                output->file_offset = layout->work[i].file_offset;
            }
        }
    }
    if (layout->scripted) {
        qsort(plan->sections, plan->count, sizeof(*plan->sections),
              minild_output_section_compare);
    }
    for (i = 0U; i < plan->count; ++i) {
        MiniLdOutputSection *output = &plan->sections[i];
        size_t j;
        if (layout->scripted && output->command != SIZE_MAX &&
            output->command < layout->script_command_count) {
            size_t command = output->command;
            uint64_t start = layout->script_outputs[command].start;
            uint64_t end = layout->script_outputs[command].end;
            size_t file_base = layout->script_outputs[command].file_base;

            plan->command_shndx[command] = i + 1U;
            if (end < start || (file_base == SIZE_MAX &&
                                 output->size != 0U &&
                                 (output->flags & SHF_ALLOC) != 0U)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-output-span:%s\n",
                        output->name);
                goto done;
            }
            if ((output->flags & SHF_ALLOC) != 0U &&
                start <= output->vaddr &&
                end >= output->vaddr &&
                output->size <= end - output->vaddr) {
                uint64_t prefix = output->vaddr - start;
                if (output->laddr < prefix) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-output-lma:%s\n",
                            output->name);
                    goto done;
                }
                output->vaddr = start;
                output->laddr -= prefix;
                output->size = end - start;
                if (file_base != SIZE_MAX) {
                    output->file_offset = file_base;
                }
            }
        }
        for (j = 0U; j < output->member_count; ++j) {
            plan->work_shndx[output->members[j]] = i + 1U;
        }
    }
    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *output = &plan->sections[i];
        size_t j;
        size_t target_index = 0U;
        for (j = 0U; j < output->member_count; ++j) {
            size_t work = output->members[j];
            const MiniLdSection *part = &state->work.sections[work];
            if (part->has_link_order_target) {
                size_t target = part->link_order_target;
                if (target >= state->work.count ||
                    plan->work_shndx[target] == 0U ||
                    (target_index != 0U &&
                     target_index != plan->work_shndx[target])) {
                    fprintf(state->diagnostics,
                            "minic-ld: incompatible-output-link-order:%s\n",
                            output->name);
                    goto done;
                }
                target_index = plan->work_shndx[target];
            }
            if (!layout->scripted || part->size == 0U ||
                part->type == SHT_NOBITS ||
                (part->flags & SHF_ALLOC) == 0U) {
                continue;
            }
            /* A single shdr cannot describe several independent file/VMA
             * displacements; reject any inconsistent span rather than
             * producing an apparently valid but wrong section table. */
            if (layout->work[work].vaddr < output->vaddr ||
                layout->work[work].laddr < output->laddr ||
                layout->work[work].file_offset < output->file_offset ||
                layout->work[work].vaddr - output->vaddr !=
                    layout->work[work].file_offset - output->file_offset ||
                layout->work[work].laddr - output->laddr !=
                    layout->work[work].vaddr - output->vaddr) {
                fprintf(state->diagnostics,
                        "minic-ld: incompatible-script-output-offset:%s\n",
                        output->name);
                goto done;
            }
        }
    }
    ok = true;

done:
    free(owner_to_output);
    if (!ok) {
        if (plan->sections == NULL || plan->work_shndx == NULL ||
            plan->work_file_offsets == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:output-section-plan\n");
        }
        minild_output_plan_destroy(plan);
    }
    return ok;
}

/* The layout phase fixes file offsets for SHF_ALLOC fragments only. Final
 * file-backed non-ALLOC sections belong to the output-section projection:
 * script members can share one output section, and a single sh_offset must
 * describe the same span as its sh_addr/sh_size. Keep these decisions out of
 * both ELF serializers. The plan owns both the working-fragment offsets
 * and each final output section's file offset. */
static bool minild_output_plan_place_nonalloc(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    MiniLdOutputPlan *plan,
    size_t first_offset,
    size_t *end_offset) {
    size_t *file_offsets = plan->work_file_offsets;
    size_t cursor = first_offset;
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        MiniLdOutputSection *output = &plan->sections[i];
        size_t base, end;
        size_t j;

        if ((output->flags & SHF_ALLOC) != 0U) {
            continue;
        }
        if (output->type == SHT_NOBITS || output->size == 0U) {
            output->file_offset = 0U;
            for (j = 0U; j < output->member_count; ++j) {
                file_offsets[output->members[j]] = 0U;
            }
            continue;
        }
        if (output->size > SIZE_MAX ||
            !align_up_size(cursor, output->align == 0U ? 1U :
                           (size_t)output->align, &base) ||
            !add_size(base, (size_t)output->size, &end)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-nonalloc-output-size:%s\n",
                    output->name);
            return false;
        }
        output->file_offset = base;
        for (j = 0U; j < output->member_count; ++j) {
            size_t work = output->members[j];
            const MiniLdSection *part = &state->work.sections[work];
            uint64_t delta;
            size_t offset;

            if (part->type == SHT_NOBITS || part->size == 0U) {
                file_offsets[work] = 0U;
                continue;
            }
            if (layout->work[work].vaddr < output->vaddr ||
                (delta = layout->work[work].vaddr - output->vaddr) >
                    SIZE_MAX - base ||
                !add_size(base + (size_t)delta, part->size, &offset) ||
                offset > end) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-nonalloc-member-span:%s\n",
                        output->name);
                return false;
            }
            file_offsets[work] = base + (size_t)delta;
        }
        cursor = end;
    }
    *end_offset = cursor;
    return true;
}

/* An output section's ELF identity and LINK_ORDER relationship have one
 * owner regardless of whether the image is ET_EXEC or ET_DYN. Dynamic
 * metadata may refine sh_link/sh_info after this common projection. */
static bool minild_output_plan_section_headers(
    const MiniLdState *state,
    const MiniLdOutputPlan *plan,
    const uint32_t *name_offsets,
    Elf64_Shdr *headers) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *section = &plan->sections[i];
        Elf64_Shdr *header = &headers[i + 1U];
        size_t j;

        if (section->type != SHT_NOBITS && section->size != 0U &&
            section->file_offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-output-file-offset:%s\n",
                    section->name);
            return false;
        }
        header->sh_name = name_offsets[i];
        header->sh_type = section->type;
        header->sh_flags = section->flags;
        header->sh_addr = (section->flags & SHF_ALLOC) != 0U
                              ? section->vaddr : 0U;
        header->sh_offset = section->type == SHT_NOBITS ||
                                    section->size == 0U
                                ? 0U : section->file_offset;
        header->sh_size = section->size;
        header->sh_addralign = section->align == 0U ? 1U : section->align;
        header->sh_entsize = section->entsize;
        for (j = 0U; j < section->member_count; ++j) {
            const MiniLdSection *part = &state->work.sections[section->members[j]];
            if (part->has_link_order_target) {
                header->sh_link = (Elf64_Word)
                    plan->work_shndx[part->link_order_target];
                break;
            }
        }
    }
    return true;
}

static bool minild_output_write_script_bytes(const MiniLdState *state,
                                              const MiniLdLayout *layout,
                                              const MiniLdOutputPlan *plan,
                                              unsigned char *image,
                                              size_t image_size) {
    size_t i;

    for (i = 0U; i < layout->script_byte_count; ++i) {
        const MiniLdScriptBytePatch *patch = &layout->script_bytes[i];
        const MiniLdOutputSection *output;
        uint64_t delta;
        size_t offset;
        size_t shndx;

        if (patch->command >= layout->script_command_count ||
            plan->command_shndx == NULL ||
            (shndx = plan->command_shndx[patch->command]) == 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-script-BYTE-output-owner\n");
            return false;
        }
        output = &plan->sections[shndx - 1U];
        if (output->type == SHT_NOBITS ||
            patch->address < output->vaddr ||
            (delta = patch->address - output->vaddr) >= output->size ||
            output->file_offset == SIZE_MAX ||
            delta > SIZE_MAX - output->file_offset ||
            (offset = output->file_offset + (size_t)delta) >= image_size) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-script-BYTE-file-offset:%s\n",
                    output->name);
            return false;
        }
        image[offset] = patch->value;
    }
    return true;
}

/* Final emission follows the OutputPlan's single surviving-section map.
 * Writers do not independently decide which work fragments survive, which
 * script gaps receive fill, or where final bytes belong. */
static bool minild_output_plan_emit_sections(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *plan,
    unsigned char *image,
    size_t image_size) {
    size_t i;

    for (i = 0U; i < plan->count; ++i) {
        const MiniLdOutputSection *output = &plan->sections[i];
        if (output->have_fill && output->fill != 0U &&
            output->type != SHT_NOBITS && output->size != 0U) {
            if (output->file_offset == SIZE_MAX ||
                output->size > SIZE_MAX ||
                !range_ok(output->file_offset, (size_t)output->size,
                          image_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-output-fill-range:%s\n",
                        output->name);
                return false;
            }
            memset(image + output->file_offset, output->fill,
                   (size_t)output->size);
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *part = &state->work.sections[i];
        size_t offset = plan->work_file_offsets[i];

        if (plan->work_shndx[i] == 0U || part->type == SHT_NOBITS ||
            part->size == 0U) {
            continue;
        }
        if (offset == SIZE_MAX ||
            !range_ok(offset, part->size, image_size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-final-section-bytes:%s\n",
                    part->name);
            return false;
        }
        memcpy(image + offset, part->data, part->size);
    }
    return minild_output_write_script_bytes(state, layout, plan,
                                             image, image_size);
}
