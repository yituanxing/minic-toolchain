/* AArch64 ET_EXEC GOT ownership.  GNU-produced ADR_GOT_PAGE/LD64_GOT_LO12
 * pairs reference the address of a pointer-sized GOT entry, not S+A.  The
 * target reserves one entry per referenced canonical symbol before layout;
 * Common assigns .got an address and emits the ordinary section. */
/* Older glibc elf.h versions omit this AAELF64 psABI relocation (315). */
#ifndef R_AARCH64_GOTPCREL32
#define R_AARCH64_GOTPCREL32 315
#endif
/* A53-843419 is an address-dependent *post-layout* target transform.  The
 * work section owns relocated code and its appended 8-byte branch islands;
 * immutable InputGraph sections never change.  Keep the patch plan alongside
 * GOT/IFUNC state so that repeated layout passes remain idempotent. */
typedef struct MiniLdAarch64ErratumPatch {
    size_t section;
    size_t instruction_offset;
    size_t stub_offset;
    uint32_t instruction;
} MiniLdAarch64ErratumPatch;

typedef struct MiniLdAarch64CodeMap {
    size_t section;
    uint64_t offset;
    bool code;
} MiniLdAarch64CodeMap;

typedef struct MiniLdAarch64ErratumPlan {
    MiniLdAarch64ErratumPatch *patches;
    size_t count;
    size_t capacity;
    size_t *original_sizes;
    unsigned char *input_owned;
    size_t original_section_count;
    MiniLdAarch64CodeMap *code_map;
    size_t code_map_count;
    bool initialized;
} MiniLdAarch64ErratumPlan;

typedef struct MiniLdAarch64StaticGot {
    size_t section;
    size_t kernel_gotplt_section;
    size_t symbol_count;
    size_t *slot_offsets;
    /* TLS IE entries hold TP-relative offsets, never absolute symbol VMAs.
     * A symbol may appear in both ordinary GOT and IE relocations. */
    size_t *tls_slot_offsets;
    /* A static IFUNC must be called through an IPLT thunk.  glibc walks
     * __rela_iplt_{start,end} at startup, invokes each resolver and installs
     * its selected implementation in the associated writable slot. */
    size_t *ifunc_stub_offsets;
    size_t *ifunc_slot_offsets;
    size_t ifunc_stub_section;
    size_t ifunc_got_section;
    size_t ifunc_rela_section;
    size_t ifunc_count;
    /* Kernel ET_DYN KASLR table; independent from static-IFUNC .rela.plt. */
    size_t kernel_rela_section;
    size_t kernel_relative_reserved_count;
    bool present;
    MiniLdAarch64ErratumPlan erratum843419;
} MiniLdAarch64StaticGot;

typedef struct MiniLdAarch64KernelRelativeRecord {
    uint64_t place;
    uint64_t addend;
} MiniLdAarch64KernelRelativeRecord;

typedef struct MiniLdAarch64GotOrder {
    size_t symbol;
    size_t original_offset;
    size_t hash_bucket;
    bool local;
} MiniLdAarch64GotOrder;

typedef struct MiniLdAarch64KernelDynRecord {
    uint64_t place;
    uint32_t type;
    size_t symbol;
    int64_t addend;
} MiniLdAarch64KernelDynRecord;

/* BFD allocates AArch64 global GOT slots through elf_link_hash_traverse,
 * rather than from the order in which input relocations were encountered.
 * The hash table grows at 75% occupancy using libiberty's prime sequence;
 * its bucket traversal controls every global GOT word and consequently the
 * immediates of all ADR_GOT_PAGE/LD64_GOT_LO12 pairs. */
static size_t minild_aarch64_bfd_hash_capacity(const MiniLdState *state) {
    static const size_t capacities[] = {
        4051U, 4093U, 8191U, 16381U, 32749U, 65521U,
        131071U, 262139U, 524287U, 1048573U, 2097143U,
        4194301U, 8388593U, 16777213U, 33554393U,
        67108859U, 134217689U, 268435399U, 536870909U
    };
    size_t nonlocal = 0U;
    size_t capacity_index = 0U;
    size_t i;

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        if (ELF64_ST_BIND(symbol->info) != STB_LOCAL &&
            symbol->name != NULL && symbol->name[0] != '\0') {
            ++nonlocal;
        }
    }
    while (capacity_index + 1U <
               sizeof(capacities) / sizeof(capacities[0]) &&
           nonlocal > (capacities[capacity_index] * 3U) / 4U) {
        ++capacity_index;
    }
    return capacities[capacity_index];
}

/* Same unsigned-long (LP64 BFD) string hash used by bfd/hash.c. */
static size_t minild_aarch64_bfd_hash_bucket(const char *name,
                                             size_t bucket_count) {
    const unsigned char *cursor = (const unsigned char *)name;
    unsigned long hash = 0UL;
    unsigned int length = 0U;

    while (*cursor != 0U) {
        unsigned long c = (unsigned long)*cursor++;
        hash += c + (c << 17U);
        hash ^= hash >> 2U;
        ++length;
    }
    hash += (unsigned long)length + ((unsigned long)length << 17U);
    hash ^= hash >> 2U;
    return (size_t)(hash % (unsigned long)bucket_count);
}

static int minild_aarch64_got_order_compare(const void *left,
                                             const void *right) {
    const MiniLdAarch64GotOrder *a = left;
    const MiniLdAarch64GotOrder *b = right;

    if (a->local != b->local) return a->local ? -1 : 1;
    if (a->local) {
        if (a->original_offset < b->original_offset) return -1;
        if (a->original_offset > b->original_offset) return 1;
    } else {
        if (a->hash_bucket < b->hash_bucket) return -1;
        if (a->hash_bucket > b->hash_bucket) return 1;
        /* BFD chains symbols in reverse discovery order within a bucket. */
        if (a->symbol > b->symbol) return -1;
        if (a->symbol < b->symbol) return 1;
    }
    return 0;
}

static int minild_aarch64_got_slot_compare(const void *left,
                                            const void *right) {
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static bool minild_aarch64_kernel_order_got(MiniLdState *state,
                                             MiniLdAarch64StaticGot *got) {
    MiniLdAarch64GotOrder *order;
    size_t *slots;
    size_t buckets = minild_aarch64_bfd_hash_capacity(state);
    size_t used = 0U;
    size_t i;

    for (i = 0U; i < got->symbol_count; ++i) {
        if (got->slot_offsets[i] != SIZE_MAX) ++used;
    }
    if (used < 2U) return true;
    if (used > SIZE_MAX / sizeof(*order) ||
        used > SIZE_MAX / sizeof(*slots)) return false;
    order = malloc(used * sizeof(*order));
    slots = malloc(used * sizeof(*slots));
    if (order == NULL || slots == NULL) {
        free(order);
        free(slots);
        return false;
    }
    used = 0U;
    for (i = 0U; i < got->symbol_count; ++i) {
        const MiniLdResolvedSymbol *symbol;
        if (got->slot_offsets[i] == SIZE_MAX) continue;
        symbol = &state->resolver.entries[i];
        order[used].symbol = i;
        order[used].original_offset = got->slot_offsets[i];
        order[used].local = ELF64_ST_BIND(symbol->info) == STB_LOCAL;
        order[used].hash_bucket = minild_aarch64_bfd_hash_bucket(
            symbol->name, buckets);
        slots[used] = got->slot_offsets[i];
        ++used;
    }
    qsort(order, used, sizeof(*order), minild_aarch64_got_order_compare);
    /* The set of reserved offsets is unchanged; only their ownership moves. */
    qsort(slots, used, sizeof(*slots), minild_aarch64_got_slot_compare);
    for (i = 0U; i < used; ++i) {
        got->slot_offsets[order[i].symbol] = slots[i];
    }
    free(order);
    free(slots);
    return true;
}

static bool minild_aarch64_kernel_undefined_weak(
    const MiniLdState *state, size_t symbol_index) {
    const MiniLdResolvedSymbol *symbol;

    if (symbol_index >= state->resolver.count) return false;
    symbol = &state->resolver.entries[symbol_index];
    return symbol->section == MINILD_SECTION_UNDEF &&
           ELF64_ST_BIND(symbol->info) == STB_WEAK &&
           ELF64_ST_VISIBILITY(symbol->other) == STV_DEFAULT;
}

/* BFD assigns dynamic indices before a kernel script discards .dynsym:
 * section-local entries precede forced-local script PROVIDEs, followed by
 * global hash traversal. A hidden UND may be eligible for that local prefix
 * only while it belongs to one input object and its value is address-taken.
 * Repeated hidden UND declarations from different ET_REL objects become
 * forced-local during early BFD hash merging and do not consume a dynindex;
 * CALL26/JUMP26-only aliases likewise do not need one. Keep the provenance
 * from original InputGraph symbols, and require an actually applied PROVIDE. */
static bool minild_aarch64_kernel_dyn_local_count(
    const MiniLdState *state, size_t *count_out) {
    unsigned char *seen = NULL, *address_referenced = NULL;
    size_t *first_file = NULL;
    size_t i, count = 0U, symbols = state->resolver.count;
    bool seen_code = false, seen_data = false;

    if (symbols > SIZE_MAX / sizeof(*first_file)) return false;
    seen = calloc(symbols == 0U ? 1U : symbols, sizeof(*seen));
    address_referenced = calloc(symbols == 0U ? 1U : symbols,
                                sizeof(*address_referenced));
    first_file = malloc((symbols == 0U ? 1U : symbols) *
                        sizeof(*first_file));
    if (seen == NULL || address_referenced == NULL || first_file == NULL) {
        free(seen);
        free(address_referenced);
        free(first_file);
        return false;
    }
    for (i = 0U; i < state->inputs.symbol_count; ++i) {
        const MiniLdInputSymbol *source = &state->inputs.symbols[i];
        const MiniLdResolvedSymbol *resolved;
        size_t canonical = source->canonical_symbol;

        if (canonical >= symbols || seen[canonical] == 2U ||
            source->elf_section != SHN_UNDEF ||
            ELF64_ST_BIND(source->info) == STB_LOCAL ||
            (ELF64_ST_VISIBILITY(source->other) != STV_HIDDEN &&
             ELF64_ST_VISIBILITY(source->other) != STV_INTERNAL)) {
            continue;
        }
        resolved = &state->resolver.entries[canonical];
        if (!resolved->script_provided ||
            resolved->section == MINILD_SECTION_UNDEF) {
            continue;
        }
        if (seen[canonical] == 0U) {
            first_file[canonical] = source->file;
            seen[canonical] = 1U;
        } else if (first_file[canonical] != source->file) {
            seen[canonical] = 2U;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        const MiniLdSection *source;
        if (reloc->symbol >= symbols || reloc->section >= state->work.count ||
            reloc->type == R_AARCH64_CALL26 ||
            reloc->type == R_AARCH64_JUMP26) continue;
        source = &state->work.sections[reloc->section];
        if ((source->flags & SHF_ALLOC) != 0U && !source->gc_discarded &&
            !source->script_discard) {
            address_referenced[reloc->symbol] = 1U;
        }
    }
    /* BFD's section-local dynsym records originate in admitted ET_REL
     * sections, not linker-created .got/.rela.dyn output buffers.  In
     * particular a GOT-only tiny fixture has a live code input but no
     * original data input; allocating a synthetic writable GOT must not
     * invent a second section-local dynindex. */
    for (i = 0U; i < state->inputs.section_count; ++i) {
        const MiniLdInputSection *input = &state->inputs.sections[i];
        const MiniLdSection *section;
        size_t owner;
        if ((input->flags & SHF_ALLOC) == 0U || input->size == 0U) continue;
        if (state->work.placements == NULL) continue;
        owner = state->work.placements[i].work_section;
        if (owner >= state->work.count) continue;
        section = &state->work.sections[owner];
        if (section->gc_discarded || section->script_discard) continue;
        if ((input->flags & SHF_EXECINSTR) != 0U) seen_code = true;
        else seen_data = true;
    }
    count = (seen_code ? 1U : 0U) + (seen_data ? 1U : 0U);
    for (i = 0U; i < symbols; ++i) {
        if (seen[i] == 1U && address_referenced[i]) ++count;
    }
    free(seen);
    free(address_referenced);
    free(first_file);
    *count_out = count;
    return true;
}

static int minild_aarch64_kernel_dyn_record_compare(const void *left,
                                                      const void *right) {
    const MiniLdAarch64KernelDynRecord *a = left;
    const MiniLdAarch64KernelDynRecord *b = right;
    /* GNU appends .rela.got's GLOB_DAT entries before input-section ABS64
     * dynamic entries, each group's records ordered by relocation place. */
    if (a->type != b->type) {
        if (a->type == R_AARCH64_GLOB_DAT) return -1;
        if (b->type == R_AARCH64_GLOB_DAT) return 1;
    }
    if (a->place < b->place) return -1;
    if (a->place > b->place) return 1;
    return 0;
}

static bool minild_aarch64_kernel_emit_weak_dynrels(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdAarch64StaticGot *got) {
    MiniLdAarch64GotOrder *dyn_symbols = NULL;
    MiniLdAarch64KernelDynRecord *records = NULL;
    size_t *dynamic_index = NULL;
    unsigned char *needed = NULL;
    size_t local_count, dynamic_count = 0U, record_count = 0U;
    size_t bucket_count = minild_aarch64_bfd_hash_capacity(state);
    size_t i;
    bool ok = false;

    if (got->kernel_rela_section == SIZE_MAX) return true;
    if (!minild_aarch64_kernel_dyn_local_count(state, &local_count) ||
        got->symbol_count > SIZE_MAX / sizeof(*dynamic_index)) return false;
    needed = calloc(got->symbol_count == 0U ? 1U :
                    got->symbol_count, sizeof(*needed));
    dynamic_index = malloc((got->symbol_count == 0U ? 1U :
                            got->symbol_count) * sizeof(*dynamic_index));
    if (needed == NULL || dynamic_index == NULL) goto out;
    for (i = 0U; i < got->symbol_count; ++i) {
        dynamic_index[i] = SIZE_MAX;
        if (got->slot_offsets[i] != SIZE_MAX &&
            minild_aarch64_kernel_undefined_weak(state, i)) {
            needed[i] = 1U;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        bool live;

        if (reloc->type != R_AARCH64_ABS64 ||
            !minild_aarch64_kernel_undefined_weak(state, reloc->symbol)) {
            continue;
        }
        if (!minild_static_reloc_source(state, layout, NULL, reloc,
                                       "aarch64-kernel-weak-source",
                                       &source, &live)) goto out;
        if (live && (source->flags & SHF_ALLOC) != 0U) {
            needed[reloc->symbol] = 1U;
        }
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        if (needed[i] != 0U) ++dynamic_count;
    }
    if (dynamic_count == 0U) {
        ok = true;
        goto out;
    }
    if (dynamic_count > SIZE_MAX / sizeof(*dyn_symbols) ||
        got->kernel_relative_reserved_count > SIZE_MAX / sizeof(*records)) {
        goto out;
    }
    dyn_symbols = malloc(dynamic_count * sizeof(*dyn_symbols));
    records = malloc(got->kernel_relative_reserved_count * sizeof(*records));
    if (dyn_symbols == NULL || records == NULL) goto out;
    dynamic_count = 0U;
    for (i = 0U; i < got->symbol_count; ++i) {
        if (needed[i] == 0U) continue;
        dyn_symbols[dynamic_count].symbol = i;
        dyn_symbols[dynamic_count].original_offset = SIZE_MAX;
        dyn_symbols[dynamic_count].local = false;
        dyn_symbols[dynamic_count].hash_bucket =
            minild_aarch64_bfd_hash_bucket(
                state->resolver.entries[i].name, bucket_count);
        ++dynamic_count;
    }
    qsort(dyn_symbols, dynamic_count, sizeof(*dyn_symbols),
          minild_aarch64_got_order_compare);
    for (i = 0U; i < dynamic_count; ++i) {
        if (i >= UINT32_MAX ||
            local_count > (size_t)UINT32_MAX - i - 1U) goto out;
        dynamic_index[dyn_symbols[i].symbol] = local_count + i + 1U;
    }
    if (got->present && got->section < state->work.count) {
        uint64_t got_base = layout->work[got->section].vaddr;
        for (i = 0U; i < got->symbol_count; ++i) {
            size_t offset = got->slot_offsets[i];
            if (offset == SIZE_MAX || dynamic_index[i] == SIZE_MAX) continue;
            if (record_count >= got->kernel_relative_reserved_count ||
                offset > UINT64_MAX - got_base) goto out;
            records[record_count].place = got_base + (uint64_t)offset;
            records[record_count].type = R_AARCH64_GLOB_DAT;
            records[record_count].symbol = i;
            records[record_count].addend = 0;
            ++record_count;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdStaticRelocOperands operands;
        MiniLdSection *source;
        bool live;

        if (reloc->type != R_AARCH64_ABS64 ||
            reloc->symbol >= got->symbol_count ||
            dynamic_index[reloc->symbol] == SIZE_MAX) continue;
        if (!minild_static_reloc_source(state, layout, NULL, reloc,
                                       "aarch64-kernel-weak-source",
                                       &source, &live)) goto out;
        if (!live || (source->flags & SHF_ALLOC) == 0U) continue;
        if (source->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
            !range_ok((size_t)reloc->offset, 8U, source->size) ||
            !minild_static_reloc_operands(
                state, layout, NULL, reloc,
                "aarch64-kernel-weak-place", &operands) ||
            record_count >= got->kernel_relative_reserved_count) goto out;
        records[record_count].place = operands.place;
        records[record_count].type = R_AARCH64_ABS64;
        records[record_count].symbol = reloc->symbol;
        records[record_count].addend = reloc->addend;
        ++record_count;
    }
    if (record_count > got->kernel_relative_reserved_count) goto out;
    qsort(records, record_count, sizeof(*records),
          minild_aarch64_kernel_dyn_record_compare);
    for (i = 0U; i < record_count; ++i) {
        MiniLdSection *rela = &state->work.sections[got->kernel_rela_section];
        size_t entry = got->kernel_relative_reserved_count - record_count + i;
        size_t offset;
        if (entry > SIZE_MAX / sizeof(Elf64_Rela)) goto out;
        offset = entry * sizeof(Elf64_Rela);
        if (!range_ok(offset, sizeof(Elf64_Rela), rela->size)) goto out;
        store_u64le(rela->data + offset, records[i].place);
        store_u64le(rela->data + offset + 8U,
                    ELF64_R_INFO(dynamic_index[records[i].symbol],
                                 records[i].type));
        store_u64le(rela->data + offset + 16U,
                    (uint64_t)records[i].addend);
    }
    ok = true;
out:
    if (!ok) fprintf(state->diagnostics,
                     "minic-ld: aarch64-kernel-weak-dynrel-invalid\n");
    free(needed);
    free(dynamic_index);
    free(dyn_symbols);
    free(records);
    return ok;
}

static int minild_aarch64_kernel_relative_compare(const void *a,
                                                   const void *b) {
    const MiniLdAarch64KernelRelativeRecord *left = a;
    const MiniLdAarch64KernelRelativeRecord *right = b;

    if (left->place < right->place) return -1;
    if (left->place > right->place) return 1;
    return 0;
}

/* The image's allocated definitions move with the KASLR slide; SHN_ABS
 * constants, discarded/non-allocated sections and undefined weak zero do
 * not.  Both RELA creation and source-byte retention share this predicate. */
static bool minild_aarch64_kernel_symbol_rebases(
    const MiniLdState *state, size_t symbol_index) {
    const MiniLdResolvedSymbol *symbol;

    if (!state->kernel_shared_image ||
        symbol_index >= state->resolver.count) {
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    return symbol->section >= 0 &&
           (size_t)symbol->section < state->work.count &&
           (state->work.sections[symbol->section].flags & SHF_ALLOC) != 0U;
}

/* AArch64 BFD emits R_RELATIVE for a non-preemptible GOT entry even when its
 * definition is SHN_ABS (including a late linker-script PROVIDE).  Direct
 * ABS64 data references to an absolute definition stay link-time constants;
 * only the GOT slot needs this extra dynamic relocation. */
static bool minild_aarch64_kernel_got_needs_relative(
    const MiniLdState *state, size_t symbol_index) {
    if (!state->kernel_shared_image ||
        symbol_index >= state->resolver.count) return false;
    return state->resolver.entries[symbol_index].section ==
               MINILD_SECTION_ABS ||
           minild_aarch64_kernel_symbol_rebases(state, symbol_index);
}

static bool minild_aarch64_kernel_relative_target(
    const MiniLdState *state, const MiniLdReloc *reloc) {
    return reloc->type == R_AARCH64_ABS64 &&
           minild_aarch64_kernel_symbol_rebases(state, reloc->symbol);
}

static bool minild_aarch64_static_uses_got(uint32_t type);

static bool minild_aarch64_static_ifunc_reference(uint32_t type) {
    switch (type) {
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    case R_AARCH64_ABS64:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADD_ABS_LO12_NC:
        return true;
    default:
        return minild_aarch64_static_uses_got(type);
    }
}

static bool minild_aarch64_static_uses_got(uint32_t type) {
    switch (type) {
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_GOTPCREL32:
        return true;
    default:
        return false;
    }
}

static bool minild_aarch64_static_uses_tls_ie(uint32_t type) {
    switch (type) {
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
        return true;
    default:
        return false;
    }
}

static bool minild_aarch64_static_uses_tls_le(uint32_t type) {
    return (type >= R_AARCH64_TLSLE_MOVW_TPREL_G2 &&
            type <= R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC) ||
           type == R_AARCH64_TLSLE_LDST128_TPREL_LO12 ||
           type == R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC;
}

/* SysV AArch64 uses TLS variant I.  TP points to a 16-byte TCB and the
 * executable's PT_TLS block follows that TCB, possibly after padding.
 * AAELF's PADsize is (PT_TLS.p_vaddr - 16) mod PT_TLS.p_align. */
static bool minild_aarch64_static_tls_offset(
    MiniLdState *state, const MiniLdLayout *layout,
    size_t symbol_index, uint64_t *offset_out) {
    const MiniLdResolvedSymbol *symbol;
    MiniLdTlsProjection tls;
    uint64_t address;
    uint64_t pad, base_mod, tcb_mod, relative;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    if (ELF64_ST_TYPE(symbol->info) != STT_TLS) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-non-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    if (symbol->section == MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(symbol->info) == STB_WEAK) {
        /* An undefined weak TLS reference carries no module allocation. */
        *offset_out = 0U;
        return true;
    }
    minild_layout_project_tls(state, layout, &tls);
    if (!tls.valid_extents || !tls.has_nonempty_section ||
        tls.alignment == 0U || symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_TLS) == 0U ||
        !minild_layout_resolve_symbol(state, layout, symbol_index, &address) ||
        address < tls.block_base || address > tls.memory_end) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-aarch64-static-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    base_mod = tls.block_base % tls.alignment;
    tcb_mod = UINT64_C(16) % tls.alignment;
    pad = base_mod >= tcb_mod ? base_mod - tcb_mod :
          tls.alignment - (tcb_mod - base_mod);
    relative = address - tls.block_base;
    if (pad > UINT64_MAX - UINT64_C(16) ||
        relative > UINT64_MAX - (UINT64_C(16) + pad)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-offset-overflow\n");
        return false;
    }
    *offset_out = UINT64_C(16) + pad + relative;
    return true;
}

static void minild_aarch64_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got = plan->target_data;

    if (got != NULL) {
        free(got->slot_offsets);
        free(got->tls_slot_offsets);
        free(got->ifunc_stub_offsets);
        free(got->ifunc_slot_offsets);
        free(got->erratum843419.patches);
        free(got->erratum843419.original_sizes);
        free(got->erratum843419.input_owned);
        free(got->erratum843419.code_map);
        free(got);
        plan->target_data = NULL;
    }
}

static bool minild_aarch64_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->kernel_gotplt_section = SIZE_MAX;
    got->ifunc_stub_section = SIZE_MAX;
    got->ifunc_got_section = SIZE_MAX;
    got->ifunc_rela_section = SIZE_MAX;
    got->kernel_rela_section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(*got->slot_offsets));
    got->tls_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                    got->symbol_count) *
                                   sizeof(*got->tls_slot_offsets));
    got->ifunc_stub_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_stub_offsets));
    got->ifunc_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_slot_offsets));
    if (got->slot_offsets == NULL || got->tls_slot_offsets == NULL ||
        got->ifunc_stub_offsets == NULL ||
        got->ifunc_slot_offsets == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:aarch64-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
        got->tls_slot_offsets[i] = SIZE_MAX;
        got->ifunc_stub_offsets[i] = SIZE_MAX;
        got->ifunc_slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        bool tls_ie = minild_aarch64_static_uses_tls_ie(reloc->type);
        size_t *offsets = tls_ie ? got->tls_slot_offsets : got->slot_offsets;
        size_t aligned;

        if (reloc->symbol != SIZE_MAX &&
            reloc->symbol < got->symbol_count &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
                STT_GNU_IFUNC) {
            MiniLdSection *stub_section;
            MiniLdSection *slot_section;
            MiniLdSection *rela_section;

            if (!minild_aarch64_static_ifunc_reference(reloc->type) ||
                state->resolver.entries[reloc->symbol].section < 0) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-aarch64-static-ifunc-reference:%u\n",
                        (unsigned)reloc->type);
                return false;
            }
            if (got->ifunc_stub_offsets[reloc->symbol] == SIZE_MAX) {
                if (!find_or_add_section(state, ".iplt", SHT_PROGBITS,
                                         SHF_ALLOC | SHF_EXECINSTR, 16U, 0U,
                                         &got->ifunc_stub_section) ||
                    !find_or_add_section(state, ".igot.plt", SHT_PROGBITS,
                                         SHF_ALLOC | SHF_WRITE, 8U, 0U,
                                         &got->ifunc_got_section) ||
                    !find_or_add_section(state, ".rela.plt", SHT_RELA,
                                         SHF_ALLOC, 8U, sizeof(Elf64_Rela),
                                         &got->ifunc_rela_section)) {
                    return false;
                }
                stub_section = &state->work.sections[got->ifunc_stub_section];
                slot_section = &state->work.sections[got->ifunc_got_section];
                rela_section = &state->work.sections[got->ifunc_rela_section];
                got->ifunc_stub_offsets[reloc->symbol] = stub_section->size;
                got->ifunc_slot_offsets[reloc->symbol] = slot_section->size;
                if (!section_append_zero(stub_section, 16U) ||
                    !section_append_zero(slot_section, 8U) ||
                    !section_append_zero(rela_section, sizeof(Elf64_Rela))) {
                    return false;
                }
                ++got->ifunc_count;
            }
        }

        if (!minild_aarch64_static_uses_got(reloc->type) && !tls_ie) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            ((tls_ie || (reloc->type != R_AARCH64_GOTREL64 &&
                         reloc->type != R_AARCH64_GOTREL32)) &&
             reloc->addend != 0)) {
            /* AAELF64 GDAT(S) relocations require a zero addend. */
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-got-relocation:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        if (tls_ie &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) !=
                STT_TLS) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-static-tls-ie-non-tls-symbol\n");
            return false;
        }
        if (offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state, ".got", SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE, 8U, 8U,
                                     &got->section)) {
                return false;
            }
            state->work.sections[got->section].gc_discarded = false;
            /* AArch64's shared-image GOT starts with the ABI-reserved
             * _GLOBAL_OFFSET_TABLE_ entry.  Ordinary non-PIE static links
             * do not need this initial header slot. */
            if (state->kernel_shared_image &&
                !section_append_zero(&state->work.sections[got->section],
                                     8U)) {
                return false;
            }
            got->present = true;
        }
        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-static-got-allocation-overflow\n");
            return false;
        }
        offsets[reloc->symbol] = aligned;
    }
    if (state->kernel_shared_image && !got->present) {
        /* GNU BFD creates the shared-image GOT header even when there are
         * no GOT-using instructions. Direct ABS64 sites still need its
         * presence to reproduce the kernel's output-section layout. */
        if (!find_or_add_section(state, ".got", SHT_PROGBITS,
                                 SHF_ALLOC | SHF_WRITE, 8U, 8U,
                                 &got->section) ||
            !section_append_zero(&state->work.sections[got->section], 8U)) {
            return false;
        }
        state->work.sections[got->section].gc_discarded = false;
        got->present = true;
    }
    if (state->kernel_shared_image && got->present &&
        !minild_aarch64_kernel_order_got(state, got)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-kernel-got-order-failed\n");
        return false;
    }
    /* AArch64 shared images reserve the first GOT word and the three-word
     * GOTPLT header even when no PLT entries are emitted. */
    if (state->kernel_shared_image && got->present) {
        MiniLdSection *gotplt;
        if (!find_or_add_section(state, ".got.plt", SHT_PROGBITS,
                                 SHF_ALLOC | SHF_WRITE, 8U, 8U,
                                 &got->kernel_gotplt_section)) {
            return false;
        }
        gotplt = &state->work.sections[got->kernel_gotplt_section];
        gotplt->gc_discarded = false;
        if (!section_append_zero(gotplt, 24U)) {
            return false;
        }
    }
    if (state->kernel_shared_image) {
        MiniLdSection *rela;

        /* Reserve after target GOT slot deduplication. Both direct ABS64
         * pointers and GOT-owned pointers need KASLR RELATIVE slots.  The
         * future script may turn some ABS64 references into absolute
         * constants, leaving harmless R_NONE capacity holes. */
        for (i = 0U; i < state->relocations.count; ++i) {
            const MiniLdReloc *reloc = &state->relocations.entries[i];
            const MiniLdSection *source;

            if (reloc->type != R_AARCH64_ABS64) continue;
            if (reloc->section >= state->work.count) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-kernel-relative-invalid-source\n");
                return false;
            }
            source = &state->work.sections[reloc->section];
            if ((source->flags & SHF_ALLOC) != 0U &&
                (source->gc_discarded || source->script_discard)) {
                /* GNU BFD charges ordinary global/weak-symbol ABS64
                 * references in allocated inputs to dynamic relocation
                 * capacity before linker-script /DISCARD/ removes those
                 * inputs.  The removed references leave R_NONE records in
                 * the reserved RELA region. Local-symbol relocations have
                 * no such global dynrel reservation.  These inert records
                 * must remain zero and follow the live RELATIVE records. */
                if (reloc->symbol < state->resolver.count &&
                    ELF64_ST_BIND(
                        state->resolver.entries[reloc->symbol].info) !=
                        STB_LOCAL) {
                    ++got->kernel_relative_reserved_count;
                }
            } else if ((source->flags & SHF_ALLOC) != 0U) {
                ++got->kernel_relative_reserved_count;
            }
        }
        for (i = 0U; i < got->symbol_count; ++i) {
            if (got->slot_offsets[i] != SIZE_MAX) {
                ++got->kernel_relative_reserved_count;
            }
        }
        if (got->kernel_relative_reserved_count != 0U) {
            if (got->kernel_relative_reserved_count >
                    SIZE_MAX / sizeof(Elf64_Rela) ||
                !find_or_add_section(state, ".rela.dyn", SHT_RELA,
                                     SHF_ALLOC, 8U, sizeof(Elf64_Rela),
                                     &got->kernel_rela_section)) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-kernel-relative-reserve-overflow\n");
                return false;
            }
            rela = &state->work.sections[got->kernel_rela_section];
            rela->gc_discarded = false;
            if (!section_append_zero(rela,
                    got->kernel_relative_reserved_count * sizeof(Elf64_Rela))) {
                return false;
            }
        }
    }
    return true;
}

static bool minild_aarch64_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-aarch64-static-plan\n");
        return false;
    }
    if (layout == NULL || layout->work == NULL ||
        (got->present && got->section >= state->work.count)) {
        return false;
    }
    if (state->kernel_shared_image) {
        /* A 32-bit absolute field cannot encode a rebased 64-bit address.
         * Linux's arm64 head emits ABS32 for script-computed image size and
         * flags; those absolute constants are not runtime relocations. */
        for (i = 0U; i < state->relocations.count; ++i) {
            const MiniLdReloc *reloc = &state->relocations.entries[i];
            MiniLdSection *source;
            bool live;

            if (reloc->type != R_AARCH64_ABS32) continue;
            if (!minild_static_reloc_source(
                    state, layout, NULL, reloc,
                    "aarch64-kernel-abs32-invalid-source", &source, &live)) {
                return false;
            }
            if (live && (source->flags & SHF_ALLOC) != 0U &&
                reloc->symbol < state->resolver.count) {
                const MiniLdResolvedSymbol *symbol =
                    &state->resolver.entries[reloc->symbol];

                if (symbol->section >= 0 &&
                    (size_t)symbol->section < state->work.count &&
                    (state->work.sections[symbol->section].flags & SHF_ALLOC) != 0U) {
                    fprintf(state->diagnostics,
                            "minic-ld: unsupported-aarch64-kernel-rebased-abs32:%s\n",
                            symbol->name);
                    return false;
                }
            }
        }
    }
    if (got->kernel_relative_reserved_count != 0U) {
        MiniLdAarch64KernelRelativeRecord *records;
        MiniLdSection *rela;
        size_t count = 0U;

        if (got->kernel_rela_section >= state->work.count) {
            return false;
        }
        rela = &state->work.sections[got->kernel_rela_section];
        records = malloc(got->kernel_relative_reserved_count *
                         sizeof(*records));
        if (records == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:aarch64-kernel-relative-records\n");
            return false;
        }
        for (i = 0U; i < state->relocations.count; ++i) {
            const MiniLdReloc *reloc = &state->relocations.entries[i];
            MiniLdStaticRelocOperands operands;
            MiniLdSection *source;
            bool live;

            if (!minild_aarch64_kernel_relative_target(state, reloc)) {
                continue;
            }
            if (!minild_static_reloc_source(state, layout, NULL, reloc,
                                           "aarch64-kernel-relative-source",
                                           &source, &live)) {
                free(records);
                return false;
            }
            if (!live || (source->flags & SHF_ALLOC) == 0U) {
                continue;
            }
            if (source->type == SHT_NOBITS ||
                reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, source->size) ||
                !minild_static_reloc_operands(
                    state, layout, NULL, reloc,
                    "aarch64-kernel-relative-place-overflow", &operands) ||
                count >= got->kernel_relative_reserved_count) {
                free(records);
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-kernel-relative-capacity-or-source\n");
                return false;
            }
            records[count].place = operands.place;
            records[count].addend = operands.target_bits;
            ++count;
        }
        if (got->present && got->section < state->work.count) {
            uint64_t got_base = layout->work[got->section].vaddr;

            for (i = 0U; i < got->symbol_count; ++i) {
                uint64_t target;
                size_t slot_offset = got->slot_offsets[i];

                if (slot_offset == SIZE_MAX ||
                    !minild_aarch64_kernel_got_needs_relative(state, i)) {
                    continue;
                }
                if (count >= got->kernel_relative_reserved_count ||
                    slot_offset > UINT64_MAX - got_base ||
                    !minild_layout_resolve_symbol(state, layout, i,
                                                  &target)) {
                    free(records);
                    fprintf(state->diagnostics,
                            "minic-ld: aarch64-kernel-got-relative-invalid\n");
                    return false;
                }
                records[count].place = got_base + (uint64_t)slot_offset;
                records[count].addend = target;
                ++count;
            }
        }
        qsort(records, count, sizeof(*records),
              minild_aarch64_kernel_relative_compare);
        for (i = 0U; i < count; ++i) {
            size_t offset = i * sizeof(Elf64_Rela);

            if (!range_ok(offset, sizeof(Elf64_Rela), rela->size)) {
                free(records);
                return false;
            }
            store_u64le(rela->data + offset, records[i].place);
            store_u64le(rela->data + offset + 8U,
                        ELF64_R_INFO(0U, R_AARCH64_RELATIVE));
            store_u64le(rela->data + offset + 16U, records[i].addend);
        }
        free(records);
    }
    if (got->present) {
        section = &state->work.sections[got->section];
        for (i = 0U; i < got->symbol_count; ++i) {
            size_t mode;

            for (mode = 0U; mode < 2U; ++mode) {
                bool tls_ie = mode != 0U;
                size_t offset = tls_ie ? got->tls_slot_offsets[i] :
                                         got->slot_offsets[i];
                uint64_t value;

                if (offset == SIZE_MAX) {
                    continue;
                }
                if (tls_ie) {
                    if (!minild_aarch64_static_tls_offset(state, layout,
                                                           i, &value)) {
                        return false;
                    }
                } else if (got->ifunc_stub_offsets[i] != SIZE_MAX) {
                    value = layout->work[got->ifunc_stub_section].vaddr +
                            (uint64_t)got->ifunc_stub_offsets[i];
                } else if (!minild_layout_resolve_symbol(state, layout,
                                                          i, &value)) {
                    return false;
                }
                /* GNU keeps the fully linked initial contents of local GOT
                 * entries even when --no-apply-dynamic-relocs is active.
                 * The paired RELATIVE record carries the same addend so the
                 * runtime loader can rebase this preinitialised pointer. */
                if (!range_ok(offset, 8U, section->size)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-aarch64-static-got-slot\n");
                    return false;
                }
                store_u64le(section->data + offset, value);
            }
        }
    }
    if (state->kernel_shared_image &&
        !minild_aarch64_kernel_emit_weak_dynrels(state, layout, got)) {
        return false;
    }
    if (got->ifunc_count != 0U) {
        MiniLdSection *stub = &state->work.sections[got->ifunc_stub_section];
        MiniLdSection *slot = &state->work.sections[got->ifunc_got_section];
        MiniLdSection *rela = &state->work.sections[got->ifunc_rela_section];
        uint64_t stub_base = layout->work[got->ifunc_stub_section].vaddr;
        uint64_t slot_base = layout->work[got->ifunc_got_section].vaddr;
        size_t rela_offset = 0U;

        for (i = 0U; i < got->symbol_count; ++i) {
            size_t stub_offset = got->ifunc_stub_offsets[i];
            size_t slot_offset = got->ifunc_slot_offsets[i];
            uint64_t resolver, stub_va, slot_va, page_delta;
            uint32_t imm;

            if (stub_offset == SIZE_MAX) {
                continue;
            }
            if (slot_offset == SIZE_MAX ||
                !range_ok(stub_offset, 16U, stub->size) ||
                !range_ok(slot_offset, 8U, slot->size) ||
                !range_ok(rela_offset, sizeof(Elf64_Rela), rela->size) ||
                (uint64_t)stub_offset > UINT64_MAX - stub_base ||
                (uint64_t)slot_offset > UINT64_MAX - slot_base ||
                !minild_layout_resolve_symbol(state, layout, i, &resolver)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-aarch64-static-ifunc-layout\n");
                return false;
            }
            stub_va = stub_base + (uint64_t)stub_offset;
            slot_va = slot_base + (uint64_t)slot_offset;
            page_delta = (slot_va >> 12U) - (stub_va >> 12U);
            if (!minild_nonriscv_signed_fits(page_delta, 21U) ||
                (slot_va & 7U) != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-static-ifunc-plt-overflow\n");
                return false;
            }
            /* ADRP X16, SLOT; LDR X17, [X16,#lo12]; BR X17; NOP. */
            imm = (uint32_t)page_delta & UINT32_C(0x1fffff);
            store_u32le(stub->data + stub_offset,
                        UINT32_C(0x90000010) |
                        ((imm & 3U) << 29U) |
                        (((imm >> 2U) & UINT32_C(0x7ffff)) << 5U));
            store_u32le(stub->data + stub_offset + 4U,
                        UINT32_C(0xf9400211) |
                        (((uint32_t)(slot_va & UINT64_C(0xfff)) >> 3U)
                         << 10U));
            store_u32le(stub->data + stub_offset + 8U, UINT32_C(0xd61f0220));
            store_u32le(stub->data + stub_offset + 12U, UINT32_C(0xd503201f));
            store_u64le(rela->data + rela_offset, slot_va);
            store_u64le(rela->data + rela_offset + 8U,
                        ELF64_R_INFO(0U, R_AARCH64_IRELATIVE));
            store_u64le(rela->data + rela_offset + 16U, resolver);
            rela_offset += sizeof(Elf64_Rela);
        }
    }
    return true;
}

static bool minild_aarch64_synthesize_default_boundaries(
    MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t base;

        if (strcmp(section->name, ".rela.plt") != 0 ||
            section->type != SHT_RELA ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        base = layout->work[i].vaddr;
        if (section->size > UINT64_MAX - base) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-static-ifunc-rela-span-overflow\n");
            return false;
        }
        return static_define_absolute_if_undefined(
                   state, "__rela_iplt_start", base) &&
               static_define_absolute_if_undefined(
                   state, "__rela_iplt_end", base + (uint64_t)section->size);
    }
    return true;
}

static bool minild_aarch64_static_resolve_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    uint64_t got_base;
    uint64_t slot;
    bool tls_ie = minild_aarch64_static_uses_tls_ie(reloc->type);
    size_t *offsets;

    if (got != NULL && reloc->symbol != SIZE_MAX &&
        reloc->symbol < got->symbol_count &&
        ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
            STT_GNU_IFUNC &&
        !minild_aarch64_static_uses_got(reloc->type)) {
        size_t stub_offset = got->ifunc_stub_offsets[reloc->symbol];

        if (stub_offset == SIZE_MAX ||
            got->ifunc_stub_section >= state->work.count ||
            !minild_aarch64_static_ifunc_reference(reloc->type) ||
            layout->work[got->ifunc_stub_section].vaddr >
                UINT64_MAX - stub_offset) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-ifunc-reference:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        *target = layout->work[got->ifunc_stub_section].vaddr +
                  (uint64_t)stub_offset + (uint64_t)reloc->addend;
        return true;
    }
    if (!minild_aarch64_static_uses_got(reloc->type) && !tls_ie) {
        if (minild_aarch64_static_uses_tls_le(reloc->type)) {
            uint64_t tls_value;

            if (reloc->symbol == SIZE_MAX ||
                !minild_aarch64_static_tls_offset(
                    state, layout, reloc->symbol, &tls_value)) {
                return false;
            }
            *target = tls_value + (uint64_t)reloc->addend;
        }
        return true;
    }
    if (got == NULL || !got->present || got->section >= state->work.count ||
        reloc->symbol >= got->symbol_count) {
        fprintf(state->diagnostics,
                "minic-ld: missing-aarch64-static-got-entry:%u\n",
                (unsigned)reloc->type);
        return false;
    }
    offsets = tls_ie ? got->tls_slot_offsets : got->slot_offsets;
    if (offsets[reloc->symbol] == SIZE_MAX ||
        layout->work[got->section].vaddr >
            UINT64_MAX - offsets[reloc->symbol]) {
        fprintf(state->diagnostics,
                "minic-ld: missing-aarch64-static-got-entry:%u\n",
                (unsigned)reloc->type);
        return false;
    }
    got_base = layout->work[got->section].vaddr;
    slot = got_base + offsets[reloc->symbol];
    switch (reloc->type) {
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
        *target = slot - got_base;
        break;
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
        *target -= got_base; /* S+A-GOT */
        break;
    case R_AARCH64_LD64_GOTOFF_LO15:
        *target = slot - got_base;
        break;
    case R_AARCH64_LD64_GOTPAGE_LO15:
        *target = slot - (got_base & ~UINT64_C(0xfff));
        break;
    case R_AARCH64_GOTPCREL32:
        *target = slot;
        break;
    default:
        *target = slot; /* GOT_LD_PREL19 / ADR_GOT_PAGE / LD64_LO12 */
        break;
    }
    return true;
}
