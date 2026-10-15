/* Linux/RISC-V --eh-frame-hdr, deliberately scoped to DWARF32 .eh_frame
 * with zR/zRS augmentation and DW_EH_PE_pcrel|sdata4 FDE pointers.
 * Unrecognized encodings fail closed instead of generating an invalid DSO.
 * ABI format: version=1, eh_ptr=pcrel|sdata4, count=udata4,
 * search_table=datarel|sdata4, sorted by the relocated initial PC.
 */
typedef struct MiniLdEhIndex {
    uint64_t pc;
    uint64_t fde;
} MiniLdEhIndex;

static bool minild_eh_skip_leb(const unsigned char *data,
                               size_t *pos, size_t end) {
    unsigned n;
    for (n = 0U; n < 10U && *pos < end; ++n) {
        unsigned char b = data[(*pos)++];
        if ((b & 0x80U) == 0U) {
            return true;
        }
    }
    return false;
}

static bool minild_eh_encoding(const MiniLdSection *section,
                               size_t cie, unsigned *encoding) {
    const unsigned char *data = section->data;
    size_t end;
    size_t pos;
    size_t augmentation;
    size_t augmentation_end;
    unsigned char version;
    bool found = false;

    if (!range_ok(cie, 8U, section->size) ||
        load_u32le(data + cie + 4U) != 0U ||
        load_u32le(data + cie) < 8U) {
        return false;
    }
    end = cie + 4U + load_u32le(data + cie);
    if (end > section->size) {
        return false;
    }
    pos = cie + 8U;
    version = data[pos++];
    augmentation = pos;
    while (pos < end && data[pos] != 0U) {
        ++pos;
    }
    if (pos == end || data[augmentation] != 'z') {
        return false;
    }
    augmentation_end = pos++;
    if (!minild_eh_skip_leb(data, &pos, end) ||
        !minild_eh_skip_leb(data, &pos, end)) {
        return false;
    }
    if (version == 1U) {
        if (pos == end) {
            return false;
        }
        ++pos;
    } else if (version == 3U || version == 4U) {
        if (!minild_eh_skip_leb(data, &pos, end)) {
            return false;
        }
    } else {
        return false;
    }
    {
        size_t size_start = pos;
        uint64_t size = 0U;
        unsigned shift = 0U;
        while (pos < end && shift <= 56U) {
            unsigned char b = data[pos++];
            size |= (uint64_t)(b & 0x7fU) << shift;
            if ((b & 0x80U) == 0U) {
                break;
            }
            shift += 7U;
        }
        if (pos <= size_start || size > end - pos) {
            return false;
        }
        /* Augmentation character 'S' is a signal-frame marker without
         * payload. Only 'R' carries the one-byte FDE pointer encoding. */
        {
            size_t n;
            size_t payload_end = pos + (size_t)size;
            for (n = augmentation + 1U; n < augmentation_end; ++n) {
                if (data[n] == 'R') {
                    if (found || pos >= payload_end) {
                        return false;
                    }
                    *encoding = data[pos++];
                    found = true;
                } else if (data[n] != 'S') {
                    return false;
                }
            }
            if (pos != payload_end) {
                return false;
            }
        }
    }
    return found;
}

static bool minild_eh_scan(MiniLdState *state,
                            size_t section_index,
                            const MiniLdStaticLayout *layout,
                            MiniLdEhIndex *result, size_t capacity,
                            size_t *count_out) {
    const MiniLdSection *section = &state->sections[section_index];
    const unsigned char *bytes = section->data;
    size_t off = 0U, count = 0U;

    while (off < section->size) {
        uint32_t length, cie_ptr;
        size_t end;
        if (!range_ok(off, 4U, section->size)) {
            goto invalid;
        }
        length = load_u32le(bytes + off);
        if (length == 0U) {
            off += 4U;
            continue;
        }
        if (length == UINT32_MAX || length < 4U ||
            !range_ok(off + 4U, length, section->size)) {
            goto invalid;
        }
        end = off + 4U + length;
        cie_ptr = load_u32le(bytes + off + 4U);
        if (cie_ptr != 0U) {
            size_t cie;
            unsigned encoding = 0U;
            uint64_t pc;
            uint64_t fde;
            int64_t disp;

            if (cie_ptr > off + 4U || length < 12U ||
                count >= UINT32_MAX) {
                goto invalid;
            }
            cie = off + 4U - cie_ptr;
            if (!minild_eh_encoding(section, cie, &encoding) ||
                encoding != 0x1bU) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-eh-frame-fde-encoding:0x%x\n",
                        encoding);
                return false;
            }
            if (layout != NULL) {
                /* The existing final-layout R_RISCV_32_PCREL pass has
                 * already resolved the signed PC-relative FDE address. */
                uint64_t location = layout->section_vaddr[section_index] +
                                    (uint64_t)(off + 8U);
                disp = (int32_t)load_u32le(bytes + off + 8U);
                if (location > (uint64_t)INT64_MAX ||
                    (disp < 0 && (uint64_t)(-disp) > location)) {
                    goto invalid;
                }
                pc = (uint64_t)((int64_t)location + disp);
                fde = layout->section_vaddr[section_index] + (uint64_t)off;
                if (count >= capacity || result == NULL) {
                    goto invalid;
                }
                result[count].pc = pc;
                result[count].fde = fde;
            }
            ++count;
        }
        off = end;
    }
    *count_out = count;
    return true;

invalid:
    fprintf(state->diagnostics, "minic-ld: malformed-eh-frame-record\n");
    return false;
}

static int minild_eh_order(const void *a, const void *b) {
    const MiniLdEhIndex *left = a, *right = b;
    if (left->pc < right->pc) {
        return -1;
    }
    if (left->pc > right->pc) {
        return 1;
    }
    return left->fde < right->fde ? -1 : left->fde > right->fde ? 1 : 0;
}

static bool minild_eh_prepare(MiniLdState *state,
                               MiniLdSharedImage *shared) {
    size_t i, count;
    for (i = 0U; i < state->section_count; ++i) {
        if (strcmp(state->sections[i].name, ".eh_frame") == 0 &&
            (state->sections[i].flags & SHF_ALLOC) != 0U &&
            state->sections[i].size != 0U) {
            break;
        }
    }
    if (i == state->section_count) {
        /* GNU LD accepts --eh-frame-hdr when no input contributes CFI.
         * Without explicit PHDRS it emits no EH header/program header;
         * with Linux vDSO's explicit PT_GNU_EH_FRAME declaration the
         * program header is present but has zero offset, address and size.
         * Do not manufacture FDEs that MiniC has not emitted.
         */
        shared->have_eh_frame_hdr = false;
        return true;
    }
    shared->eh_frame_section = i;
    if (!minild_eh_scan(state, i, NULL, NULL, 0U, &count) ||
        count > (SIZE_MAX - 12U) / 8U ||
        !find_or_add_section(state, ".eh_frame_hdr", SHT_PROGBITS,
                             SHF_ALLOC, 4U, 0U,
                             &shared->eh_frame_hdr_section) ||
        !section_append_zero(&state->sections[shared->eh_frame_hdr_section],
                             12U + count * 8U)) {
        return false;
    }
    shared->eh_frame_fde_count = count;
    shared->have_eh_frame_hdr = true;
    return true;
}

static bool minild_eh_fill(MiniLdState *state,
                            const MiniLdStaticLayout *layout,
                            const MiniLdSharedImage *shared) {
    MiniLdSection *header;
    MiniLdEhIndex *index;
    uint64_t base, frame;
    int64_t ptr;
    size_t count, i;
    bool ok = false;
    if (!shared->have_eh_frame_hdr) {
        return true;
    }
    header = &state->sections[shared->eh_frame_hdr_section];
    base = layout->section_vaddr[shared->eh_frame_hdr_section];
    frame = layout->section_vaddr[shared->eh_frame_section];
    index = calloc(shared->eh_frame_fde_count == 0U
                   ? 1U : shared->eh_frame_fde_count, sizeof(*index));
    if (index == NULL) {
        return false;
    }
    if (!minild_eh_scan(state, shared->eh_frame_section, layout, index,
                        shared->eh_frame_fde_count, &count) ||
        count != shared->eh_frame_fde_count ||
        base > INT64_MAX - 4U || frame > INT64_MAX) {
        goto out;
    }
    ptr = (int64_t)frame - (int64_t)(base + 4U);
    if (ptr < INT32_MIN || ptr > INT32_MAX) {
        goto out;
    }
    memset(header->data, 0, header->size);
    header->data[0] = 1U;
    header->data[1] = 0x1bU;
    header->data[2] = 0x03U;
    header->data[3] = 0x3bU;
    store_u32le(header->data + 4U, (uint32_t)(int32_t)ptr);
    store_u32le(header->data + 8U, (uint32_t)count);
    qsort(index, count, sizeof(*index), minild_eh_order);
    for (i = 0U; i < count; ++i) {
        int64_t pc_diff, fde_diff;
        if (index[i].pc > INT64_MAX || index[i].fde > INT64_MAX) {
            goto out;
        }
        pc_diff = (int64_t)index[i].pc - (int64_t)base;
        fde_diff = (int64_t)index[i].fde - (int64_t)base;
        if (pc_diff < INT32_MIN || pc_diff > INT32_MAX ||
            fde_diff < INT32_MIN || fde_diff > INT32_MAX) {
            goto out;
        }
        store_u32le(header->data + 12U + i * 8U,
                    (uint32_t)(int32_t)pc_diff);
        store_u32le(header->data + 16U + i * 8U,
                    (uint32_t)(int32_t)fde_diff);
    }
    ok = true;
out:
    if (!ok) {
        fprintf(state->diagnostics, "minic-ld: invalid-eh-frame-hdr-layout\n");
    }
    free(index);
    return ok;
}
