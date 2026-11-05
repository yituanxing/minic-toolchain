/* Final links are allowed to fold SHF_MERGE atoms, but only after GC and
 * linker-script input ownership are known.  A source atom's bytes, ordinary
 * symbols and the addends of STT_SECTION-relative references must receive
 * the same piecewise coordinate transformation.  This pass is deliberately
 * absent from -r and from dynamic/shared links.
 *
 * The immutable InputGraph is not rewritten.  Work sections and their
 * canonical resolver/relocation coordinates alone are transformed.  An
 * input fragment containing relocation *write sites* is left untouched:
 * deleting one would require relocation-piece deduplication, not merely a
 * target-S remap. */

typedef struct MiniLdMergePiece {
    uint64_t source;
    uint64_t length;
    uint64_t destination;
} MiniLdMergePiece;

typedef struct MiniLdMergeSectionMap {
    size_t leader;
    MiniLdMergePiece *pieces;
    size_t count;
    size_t capacity;
} MiniLdMergeSectionMap;

typedef struct MiniLdMergeAtomSlot {
    uint64_t hash;
    size_t offset;
    size_t length;
    bool occupied;
} MiniLdMergeAtomSlot;

typedef struct MiniLdMergeInputAtom {
    size_t section;
    size_t source;
    size_t length;
    size_t alignment;
    size_t canonical;
} MiniLdMergeInputAtom;

/* GNU BFD's merge.c first de-duplicates *whole* input atoms, chooses suffix
 * owners with a reverse-lexicographic ordering, and only then packs the
 * surviving atoms in first-encounter order.  The suffix-sort order is not
 * the output-byte order. */
typedef struct MiniLdMergeUniqueAtom {
    size_t section;
    size_t source;
    size_t length;
    size_t alignment;
    size_t destination;
    size_t suffix_owner;
} MiniLdMergeUniqueAtom;

typedef struct MiniLdMergeSortedAtom {
    const unsigned char *bytes;
    size_t length;
    size_t alignment;
    size_t tail_mod;
    size_t canonical;
} MiniLdMergeSortedAtom;

typedef struct MiniLdMergeGroup {
    size_t leader;
    size_t owner;
    uint64_t flags;
    uint64_t align;
    uint64_t entsize;
    unsigned char *data;
    size_t size;
    size_t capacity;
    MiniLdMergeAtomSlot *slots;
    size_t slot_count;
    size_t slot_capacity;
    MiniLdMergeInputAtom *atoms;
    size_t atom_count;
    size_t atom_capacity;
    MiniLdMergeUniqueAtom *unique;
    size_t unique_count;
    bool pad_trailing;
} MiniLdMergeGroup;

/* Build a complete pool before folding it.  Online insertion misses the
 * common case in which a short string precedes its longer containing
 * suffix: GNU linkers see the entire merge set before assigning offsets. */
static bool minild_merge_gather_atom(MiniLdMergeGroup *group,
                                     size_t section, size_t source,
                                     size_t length, size_t alignment) {
    MiniLdMergeInputAtom *next;
    size_t capacity;
    if (group->atom_count == group->atom_capacity) {
        capacity = group->atom_capacity == 0U ? 64U :
                   group->atom_capacity * 2U;
        if (capacity < group->atom_capacity ||
            capacity > SIZE_MAX / sizeof(*next)) {
            return false;
        }
        next = realloc(group->atoms, capacity * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        group->atoms = next;
        group->atom_capacity = capacity;
    }
    group->atoms[group->atom_count++] =
        (MiniLdMergeInputAtom){section, source, length, alignment, SIZE_MAX};
    return true;
}

static int minild_merge_reverse_atom_compare(const void *left,
                                              const void *right) {
    const MiniLdMergeSortedAtom *a = left, *b = right;
    size_t i;
    size_t limit = a->length < b->length ? a->length : b->length;
    if (a->tail_mod != b->tail_mod) {
        return a->tail_mod < b->tail_mod ? -1 : 1;
    }
    for (i = 0U; i < limit; ++i) {
        unsigned char av = a->bytes[a->length - 1U - i];
        unsigned char bv = b->bytes[b->length - 1U - i];
        if (av != bv) {
            return av < bv ? -1 : 1;
        }
    }
    return a->length == b->length ? 0 :
           a->length < b->length ? -1 : 1;
}

static int minild_merge_piece_compare(const void *left, const void *right) {
    const MiniLdMergePiece *a = left, *b = right;
    if (a->source == b->source) {
        return 0;
    }
    return a->source < b->source ? -1 : 1;
}

static bool minild_merge_add_signed(uint64_t value, int64_t addend,
                                     uint64_t *result) {
    if (addend >= 0) {
        uint64_t magnitude = (uint64_t)addend;
        if (value > UINT64_MAX - magnitude) {
            return false;
        }
        *result = value + magnitude;
    } else {
        uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1U;
        if (value < magnitude) {
            return false;
        }
        *result = value - magnitude;
    }
    return true;
}

static uint64_t minild_merge_reverse_hash(const unsigned char *data,
                                          size_t size) {
    uint64_t h = UINT64_C(1469598103934665603);
    while (size != 0U) {
        h = (h * UINT64_C(1099511628211)) ^ data[--size];
    }
    return h;
}

static bool minild_merge_grow_slots(MiniLdMergeGroup *group,
                                    size_t requested) {
    MiniLdMergeAtomSlot *slots;
    size_t capacity = group->slot_capacity == 0U
                          ? 128U : group->slot_capacity;
    size_t i;

    if (requested <= group->slot_capacity / 2U) {
        return true;
    }
    while (requested > capacity / 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < group->slot_capacity; ++i) {
        const MiniLdMergeAtomSlot *old = &group->slots[i];
        size_t index;
        if (!old->occupied) {
            continue;
        }
        index = (size_t)old->hash & (capacity - 1U);
        while (slots[index].occupied) {
            index = (index + 1U) & (capacity - 1U);
        }
        slots[index] = *old;
    }
    free(group->slots);
    group->slots = slots;
    group->slot_capacity = capacity;
    return true;
}

static bool minild_merge_lookup_unique(const MiniLdMergeGroup *group,
                                       const MiniLdState *state,
                                       const unsigned char *bytes,
                                       size_t length, uint64_t hash,
                                       size_t *canonical) {
    size_t i;
    if (group->slot_capacity == 0U) {
        return false;
    }
    i = (size_t)hash & (group->slot_capacity - 1U);
    while (group->slots[i].occupied) {
        const MiniLdMergeAtomSlot *slot = &group->slots[i];
        const MiniLdMergeUniqueAtom *atom = &group->unique[slot->offset];
        if (slot->hash == hash && slot->length == length &&
            memcmp(state->work.sections[atom->section].data + atom->source,
                   bytes, length) == 0) {
            *canonical = slot->offset;
            return true;
        }
        i = (i + 1U) & (group->slot_capacity - 1U);
    }
    return false;
}

static void minild_merge_insert_atom(MiniLdMergeGroup *group,
                                     uint64_t hash, size_t offset,
                                     size_t length) {
    size_t i = (size_t)hash & (group->slot_capacity - 1U);
    while (group->slots[i].occupied) {
        i = (i + 1U) & (group->slot_capacity - 1U);
    }
    group->slots[i] = (MiniLdMergeAtomSlot){hash, offset, length, true};
    ++group->slot_count;
}

static bool minild_merge_append_bytes(MiniLdMergeGroup *group,
                                      const unsigned char *bytes,
                                      size_t size, size_t alignment,
                                      size_t *offset) {
    size_t aligned, next_size, capacity;
    unsigned char *next;

    if (!align_up_size(group->size, alignment, &aligned) ||
        !add_size(aligned, size, &next_size)) {
        return false;
    }
    if (next_size > group->capacity) {
        capacity = group->capacity == 0U ? 128U : group->capacity;
        while (capacity < next_size) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = next_size;
                break;
            }
            capacity *= 2U;
        }
        next = realloc(group->data, capacity);
        if (next == NULL) {
            return false;
        }
        group->data = next;
        group->capacity = capacity;
    }
    if (aligned != group->size) {
        memset(group->data + group->size, 0, aligned - group->size);
    }
    memcpy(group->data + aligned, bytes, size);
    *offset = aligned;
    group->size = next_size;
    return true;
}

/* Insert each distinct atom exactly once while retaining its earliest input
 * position; repeated entries only strengthen the canonical alignment. */
static bool minild_merge_register_unique(MiniLdMergeGroup *group,
                                         MiniLdState *state,
                                         MiniLdMergeInputAtom *atom) {
    const MiniLdSection *source = &state->work.sections[atom->section];
    const unsigned char *bytes = source->data + atom->source;
    const uint64_t hash = minild_merge_reverse_hash(bytes, atom->length);
    size_t canonical;

    if (minild_merge_lookup_unique(group, state, bytes, atom->length,
                                   hash, &canonical)) {
        MiniLdMergeUniqueAtom *unique = &group->unique[canonical];
        if (unique->alignment < atom->alignment) {
            unique->alignment = atom->alignment;
        }
        atom->canonical = canonical;
        return true;
    }
    if (group->unique_count == group->atom_count ||
        !minild_merge_grow_slots(group, group->slot_count + 1U)) {
        return false;
    }
    canonical = group->unique_count++;
    group->unique[canonical] =
        (MiniLdMergeUniqueAtom){atom->section, atom->source,
                                atom->length, atom->alignment, 0U,
                                SIZE_MAX};
    atom->canonical = canonical;
    minild_merge_insert_atom(group, hash, canonical, atom->length);
    return true;
}

static bool minild_merge_map_add(MiniLdMergeSectionMap *map,
                                 uint64_t source, uint64_t length,
                                 uint64_t destination);

static bool minild_merge_assign_group(MiniLdMergeGroup *group,
                                      MiniLdState *state,
                                      MiniLdMergeSectionMap *maps) {
    MiniLdMergeSortedAtom *sorted = NULL;
    size_t width = (size_t)group->entsize;
    size_t uniform_align = 0U;
    size_t i;
    bool ok = false;

    if (group->atom_count > SIZE_MAX / sizeof(*group->unique)) {
        return false;
    }
    group->unique = calloc(group->atom_count, sizeof(*group->unique));
    if (group->unique == NULL) {
        return false;
    }
    for (i = 0U; i < group->atom_count; ++i) {
        if (!minild_merge_register_unique(group, state, &group->atoms[i])) {
            return false;
        }
    }
    if ((group->flags & SHF_STRINGS) != 0U) {
        if (group->unique_count > SIZE_MAX / sizeof(*sorted)) {
            return false;
        }
        sorted = malloc(group->unique_count * sizeof(*sorted));
        if (sorted == NULL) {
            return false;
        }
        for (i = 0U; i < group->unique_count; ++i) {
            const MiniLdMergeUniqueAtom *atom = &group->unique[i];
            const MiniLdSection *section =
                &state->work.sections[atom->section];
            sorted[i].canonical = i;
            sorted[i].bytes = section->data + atom->source;
            sorted[i].length = atom->length - width;
            sorted[i].alignment = atom->alignment;
            if (uniform_align == 0U) {
                uniform_align = atom->alignment;
            } else if (uniform_align != atom->alignment) {
                uniform_align = SIZE_MAX;
            }
        }
        for (i = 0U; i < group->unique_count; ++i) {
            sorted[i].tail_mod =
                uniform_align != SIZE_MAX && uniform_align > width
                    ? sorted[i].length & (uniform_align - 1U) : 0U;
        }
        qsort(sorted, group->unique_count, sizeof(*sorted),
              minild_merge_reverse_atom_compare);
        if (group->unique_count != 0U) {
            size_t owner = sorted[group->unique_count - 1U].canonical;
            for (i = group->unique_count - 1U; i != 0U; --i) {
                const MiniLdMergeSortedAtom *candidate = &sorted[i - 1U];
                MiniLdMergeUniqueAtom *small =
                    &group->unique[candidate->canonical];
                const MiniLdMergeUniqueAtom *large = &group->unique[owner];
                const unsigned char *large_bytes =
                    state->work.sections[large->section].data + large->source;
                const unsigned char *small_bytes = candidate->bytes;
                if (large->length > small->length &&
                    large->alignment >= small->alignment &&
                    ((large->length - small->length) &
                     (small->alignment - 1U)) == 0U &&
                    memcmp(large_bytes + large->length - small->length,
                           small_bytes, small->length) == 0) {
                    small->suffix_owner = owner;
                } else {
                    owner = candidate->canonical;
                }
            }
        }
    }
    /* GNU's suffix sorter does not determine packing order: the surviving
     * unique atoms go out in their original first-encounter order. */
    for (i = 0U; i < group->unique_count; ++i) {
        MiniLdMergeUniqueAtom *atom = &group->unique[i];
        const MiniLdSection *source = &state->work.sections[atom->section];
        if (atom->suffix_owner != SIZE_MAX) {
            continue;
        }
        if (!minild_merge_append_bytes(
                group, source->data + atom->source, atom->length,
                atom->alignment, &atom->destination)) {
            goto done;
        }
    }
    for (i = 0U; i < group->unique_count; ++i) {
        MiniLdMergeUniqueAtom *atom = &group->unique[i];
        if (atom->suffix_owner != SIZE_MAX) {
            const MiniLdMergeUniqueAtom *parent =
                &group->unique[atom->suffix_owner];
            /* The backward suffix scan never promotes a folded candidate
             * to owner: GNU BFD's `e = cmp` only occurs on a failed fold.
             * Consequently each parent already has a packed destination;
             * no parent-of-parent chain or second adjustment is possible. */
            if (parent->suffix_owner != SIZE_MAX) {
                goto done;
            }
            atom->destination =
                parent->destination + parent->length - atom->length;
        }
    }
    /* BFD pads the merged output to sh_addralign if *all* original
     * participating input sections were individually padded to that
     * alignment.  For an unpadded input it leaves the output exact-sized. */
    if (group->pad_trailing) {
        const unsigned char zero = 0U;
        size_t unused_offset;
        if (!minild_merge_append_bytes(group, &zero, 0U,
                                       (size_t)group->align,
                                       &unused_offset)) {
            goto done;
        }
    }
    for (i = 0U; i < group->atom_count; ++i) {
        const MiniLdMergeInputAtom *atom = &group->atoms[i];
        if (!minild_merge_map_add(&maps[atom->section], atom->source,
                                  atom->length,
                                  group->unique[atom->canonical].destination)) {
            goto done;
        }
    }
    ok = true;
done:
    free(sorted);
    return ok;
}

static bool minild_merge_map_add(MiniLdMergeSectionMap *map,
                                 uint64_t source, uint64_t length,
                                 uint64_t destination) {
    MiniLdMergePiece *next;
    size_t capacity;
    if (map->count == map->capacity) {
        capacity = map->capacity == 0U ? 16U : map->capacity * 2U;
        if (capacity < map->capacity ||
            capacity > SIZE_MAX / sizeof(*next)) {
            return false;
        }
        next = realloc(map->pieces, capacity * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        map->pieces = next;
        map->capacity = capacity;
    }
    map->pieces[map->count++] = (MiniLdMergePiece){source, length,
                                                    destination};
    return true;
}

/* An atom at source offset P inherits gcd(sh_addralign, P) alignment, not
 * necessarily the full input-section alignment.  In particular GCC's
 * .rodata.str1.8 sections may contain successive 1-byte strings inside an
 * 8-byte-aligned fragment.  Requiring 8-byte alignment on every string
 * inserts phantom padding and shifts Linux's entire rodata output. */
static size_t minild_merge_atom_alignment(size_t section_align,
                                          size_t source_offset) {
    size_t left = section_align, right = source_offset;
    while (right != 0U) {
        size_t rem = left % right;
        left = right;
        right = rem;
    }
    return left == 0U ? 1U : left;
}

static bool minild_merge_translate(const MiniLdMergeSectionMap *map,
                                   uint64_t offset, uint64_t *result) {
    size_t lo = 0U, hi = map->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        const MiniLdMergePiece *part = &map->pieces[mid];
        if (offset < part->source) {
            hi = mid;
        } else if (offset >= part->source + part->length) {
            lo = mid + 1U;
        } else {
            *result = part->destination + offset - part->source;
            return true;
        }
    }
    return false;
}

static bool minild_merge_remap_addend(
    const MiniLdMergeSectionMap *map, uint64_t source,
    int64_t old_addend, int64_t *result) {
    uint64_t original_target, mapped_symbol, mapped_target;
    uint64_t distance;

    if (!minild_merge_add_signed(source, old_addend, &original_target) ||
        !minild_merge_translate(map, source, &mapped_symbol) ||
        !minild_merge_translate(map, original_target, &mapped_target)) {
        return false;
    }
    if (mapped_target >= mapped_symbol) {
        distance = mapped_target - mapped_symbol;
        if (distance > INT64_MAX) {
            return false;
        }
        *result = (int64_t)distance;
    } else {
        distance = mapped_symbol - mapped_target;
        if (distance > UINT64_C(0x8000000000000000)) {
            return false;
        }
        /* Avoid negating INT64_MIN or converting 2^63 to signed directly. */
        *result = -((int64_t)(distance - 1U)) - 1;
    }
    return true;
}

static size_t minild_merge_output_owner(const MiniLdState *state,
                                        const MiniLdScript *script,
                                        size_t work_index) {
    const MiniLdSection *work = &state->work.sections[work_index];
    size_t i;

    if (script == NULL) {
        /* Unscripted default sections are organized by source name. The
         * grouping pass checks that name separately before folding. */
        return 0U;
    }
    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *cmd = &script->commands[i];
        size_t j;
        if (cmd->kind != MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        for (j = 0U; j < cmd->value.section.item_count; ++j) {
            const MiniLdScriptSectionItem *item =
                &cmd->value.section.items[j];
            if (item->kind == MINILD_SCRIPT_SECTION_PATTERN &&
                !item->value.pattern.common &&
                minild_script_pattern_matches_input(
                    &item->value.pattern,
                    work->input_name != NULL ? work->input_name : work->name,
                    work->input_path,
                    work->input_archive_path,
                    work->input_archive_member_name)) {
                return cmd->value.section.discard ? SIZE_MAX : i;
            }
        }
    }
    return SIZE_MAX;
}

static bool minild_merge_live_sections(MiniLdState *state,
                                       const MiniLdScript *script) {
    MiniLdMergeSectionMap *maps = NULL;
    MiniLdMergeGroup *groups = NULL;
    bool *has_writes = NULL;
    bool *unsafe_target = NULL;
    size_t group_count = 0U;
    size_t total_saved = 0U;
    size_t original_bytes = 0U;
    size_t canonical_bytes = 0U;
    size_t i, j;
    bool ok = false;

    if (state->work.count == 0U) {
        return true;
    }
    maps = calloc(state->work.count, sizeof(*maps));
    groups = calloc(state->work.count, sizeof(*groups));
    has_writes = calloc(state->work.count, sizeof(*has_writes));
    unsafe_target = calloc(state->work.count, sizeof(*unsafe_target));
    if (maps == NULL || groups == NULL || has_writes == NULL ||
        unsafe_target == NULL) {
        goto done;
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const size_t section = state->relocations.entries[i].section;
        if (section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: merge-invalid-reloc-write-section:%zu\n",
                    section);
            goto invalid;
        }
        has_writes[section] = true;
        if (state->relocations.entries[i].symbol < state->resolver.count) {
            const MiniLdReloc *reloc = &state->relocations.entries[i];
            const MiniLdResolvedSymbol *sym =
                &state->resolver.entries[reloc->symbol];
            uint64_t target_offset;

            if (ELF64_ST_TYPE(sym->info) != STT_SECTION ||
                sym->section < 0 ||
                (size_t)sym->section >= state->work.count) {
                continue;
            }
            /* Non-plain relocation addends can encode PC bias, page
             * rounding, instruction split fields or TLS arithmetic.
             * Preserve their source section byte-for-byte.  Targets
             * decide eligibility; Common never switches on ISA numbers. */
            if (state->target->merge_section_addend_is_plain == NULL ||
                !state->target->merge_section_addend_is_plain(reloc->type) ||
                !minild_merge_add_signed(sym->value, reloc->addend,
                                          &target_offset) ||
                target_offset >=
                    state->work.sections[sym->section].size) {
                unsafe_target[sym->section] = true;
            }
        }
    }
    /* One-past-end and synthetic labels have no atom owner.  Such an
     * input section remains byte-for-byte intact instead of allowing a
     * later symbol-remap failure to abort an otherwise valid link. */
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *sym = &state->resolver.entries[i];
        if (sym->section >= 0 &&
            (size_t)sym->section < state->work.count &&
            sym->value >= state->work.sections[sym->section].size) {
            unsafe_target[sym->section] = true;
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *source = &state->work.sections[i];
        MiniLdMergeGroup *group;
        MiniLdMergeSectionMap *map = &maps[i];
        size_t owner;
        size_t pos;

        map->leader = SIZE_MAX;
        if (source->gc_discarded || source->script_discard ||
            source->input_name == NULL || has_writes[i] ||
            unsafe_target[i] ||
            (source->flags & (SHF_MERGE | SHF_ALLOC)) !=
                (SHF_MERGE | SHF_ALLOC) || source->type != SHT_PROGBITS ||
            source->size == 0U || source->data == NULL ||
            source->has_group || source->has_link_order_target ||
            source->entsize == 0U || source->entsize > SIZE_MAX ||
            ((source->flags & SHF_STRINGS) != 0U &&
                source->entsize != 1U && source->entsize != 2U &&
                source->entsize != 4U)) {
            continue;
        }
        owner = minild_merge_output_owner(state, script, i);
        if (owner == SIZE_MAX) {
            continue;
        }
        for (j = 0U; j < group_count; ++j) {
            if (groups[j].owner == owner &&
                groups[j].flags == source->flags &&
                groups[j].entsize == source->entsize &&
                /* GNU BFD merge.c partitions SEC_MERGE by *input section
                 * alignment_power*, entity width, flags and output owner.
                 * A script's .rodata command does not let 1- and 8-aligned
                 * string fragments deduplicate each other, while two
                 * differently named fragments with equal alignment may
                 * legitimately share one atom namespace. */
                groups[j].align == source->align) {
                break;
            }
        }
        if (j == group_count) {
            groups[j].leader = i;
            groups[j].owner = owner;
            groups[j].flags = source->flags;
            groups[j].align = source->align;
            groups[j].entsize = source->entsize;
            groups[j].pad_trailing = true;
            ++group_count;
        }
        group = &groups[j];
        /* By construction this pool has one input sh_addralign.  Individual
         * atoms still inherit gcd(sh_addralign, source_offset). */
        if (source->align == 0U ||
            source->size % (size_t)source->align != 0U) {
            group->pad_trailing = false;
        }
        map->leader = group->leader;
        for (pos = 0U; pos < source->size;) {
            size_t length;
            if ((source->flags & SHF_STRINGS) == 0U) {
                length = (size_t)source->entsize;
                if (length > source->size - pos) {
                    fprintf(state->diagnostics,
                            "minic-ld: merge-invalid-atom-size:section=%zu:%s:offset=%zu:entsize=%llu\n",
                            i, source->input_name, pos,
                            (unsigned long long)source->entsize);
                    goto invalid;
                }
            } else {
                size_t scan = pos;
                const size_t width = (size_t)source->entsize;
                bool terminated = false;
                if ((source->size - pos) % width != 0U) {
                    fprintf(state->diagnostics,
                            "minic-ld: merge-invalid-string-width:section=%zu:%s:size=%zu:width=%zu\n",
                            i, source->input_name, source->size, width);
                    goto invalid;
                }
                do {
                    size_t k;
                    bool zero = true;
                    if (scan > source->size - width) {
                        fprintf(state->diagnostics,
                                "minic-ld: merge-unterminated-string:section=%zu:%s:offset=%zu\n",
                                i, source->input_name, pos);
                        goto invalid;
                    }
                    for (k = 0U; k < width; ++k) {
                        zero &= source->data[scan + k] == 0U;
                    }
                    scan += width;
                    if (zero) {
                        terminated = true;
                        break;
                    }
                } while (scan < source->size);
                if (!terminated) {
                    fprintf(state->diagnostics,
                            "minic-ld: merge-unterminated-string:section=%zu:%s:offset=%zu\n",
                            i, source->input_name, pos);
                    goto invalid;
                }
                length = scan - pos;
            }
            if (!minild_merge_gather_atom(
                    group, i, pos, length,
                    minild_merge_atom_alignment((size_t)source->align,
                                                pos))) {
                goto done;
            }
            pos += length;
        }
    }
    for (i = 0U; i < group_count; ++i) {
        MiniLdMergeGroup *group = &groups[i];
        if (!minild_merge_assign_group(group, state, maps)) {
            goto done;
        }
    }
    /* Binary-search source coordinates, not the physically reordered atom
     * packing order. */
    for (i = 0U; i < state->work.count; ++i) {
        if (maps[i].count > 1U) {
            qsort(maps[i].pieces, maps[i].count, sizeof(maps[i].pieces[0]),
                  minild_merge_piece_compare);
        }
    }
    /* Validate every reference before changing any owner, including addends
     * that point inside (or to a suffix of) a mergeable atom. */
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *sym = &state->resolver.entries[i];
        uint64_t mapped = 0U;
        if (sym->section >= 0 && (size_t)sym->section < state->work.count &&
            maps[sym->section].leader != SIZE_MAX &&
            !minild_merge_translate(&maps[sym->section], sym->value,
                                    &mapped)) {
            fprintf(state->diagnostics,
                    "minic-ld: merge-symbol-unmapped:work=%d:%s:symbol=%s:value=0x%llx:size=%zu\n",
                    sym->section, state->work.sections[sym->section].input_name,
                    sym->name, (unsigned long long)sym->value,
                    state->work.sections[sym->section].size);
            goto invalid;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        const MiniLdResolvedSymbol *sym;
        const MiniLdMergeSectionMap *map;
        int64_t updated;
        if (reloc->symbol >= state->resolver.count) {
            continue;
        }
        sym = &state->resolver.entries[reloc->symbol];
        if (ELF64_ST_TYPE(sym->info) != STT_SECTION ||
            sym->section < 0 ||
            (size_t)sym->section >= state->work.count) {
            continue;
        }
        map = &maps[sym->section];
        if (map->leader == SIZE_MAX) {
            continue;
        }
        if (!minild_merge_remap_addend(map, sym->value, reloc->addend,
                                      &updated)) {
            fprintf(state->diagnostics,
                    "minic-ld: merge-target-unmapped:work=%d:%s:symbol=%s:value=0x%llx:addend=%lld:type=%u:size=%zu\n",
                    sym->section, state->work.sections[sym->section].input_name,
                    sym->name, (unsigned long long)sym->value,
                    (long long)reloc->addend, (unsigned)reloc->type,
                    state->work.sections[sym->section].size);
            goto invalid;
        }
    }
    /* All preflight succeeded: commit working byte owners and canonical
     * coordinate transforms as one logical unit. */
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        const MiniLdResolvedSymbol *sym;
        const MiniLdMergeSectionMap *map;
        int64_t updated;
        if (reloc->symbol >= state->resolver.count) {
            continue;
        }
        sym = &state->resolver.entries[reloc->symbol];
        if (ELF64_ST_TYPE(sym->info) != STT_SECTION ||
            sym->section < 0 ||
            (size_t)sym->section >= state->work.count ||
            (map = &maps[sym->section])->leader == SIZE_MAX) {
            continue;
        }
        (void)minild_merge_remap_addend(map, sym->value, reloc->addend,
                                       &updated);
        reloc->addend = updated;
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *sym = &state->resolver.entries[i];
        const MiniLdMergeSectionMap *map;
        uint64_t mapped = 0U;
        if (sym->section < 0 || (size_t)sym->section >= state->work.count ||
            (map = &maps[sym->section])->leader == SIZE_MAX) {
            continue;
        }
        (void)minild_merge_translate(map, sym->value, &mapped);
        sym->section = (int)map->leader;
        sym->value = mapped;
    }
    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        const MiniLdMergeSectionMap *map = &maps[i];
        if (map->leader == SIZE_MAX) {
            continue;
        }
        if (original_bytes <= SIZE_MAX - section->size) {
            original_bytes += section->size;
        }
        if (map->leader != i) {
            section->gc_discarded = true;
            section->size = 0U;
            continue;
        }
        for (j = 0U; j < group_count; ++j) {
            if (groups[j].leader == i) {
                free(section->data);
                section->data = groups[j].data;
                section->size = groups[j].size;
                section->capacity = groups[j].capacity;
                if (groups[j].align > section->align) {
                    section->align = groups[j].align;
                }
                groups[j].data = NULL;
                break;
            }
        }
    }
    for (i = 0U; i < group_count; ++i) {
        if (canonical_bytes <= SIZE_MAX - groups[i].size) {
            canonical_bytes += groups[i].size;
        }
    }
    total_saved = original_bytes > canonical_bytes
                      ? original_bytes - canonical_bytes : 0U;
    if (group_count != 0U) {
        fprintf(state->diagnostics,
                "minic-ld: merge-live-entries:groups=%zu:deduplicated-bytes=%zu\n",
                group_count, total_saved);
    }
    ok = true;
    goto done;
invalid:
    fprintf(state->diagnostics,
            "minic-ld: invalid-live-merge-entry-coordinate\n");
done:
    if (maps != NULL) {
        for (i = 0U; i < state->work.count; ++i) {
            free(maps[i].pieces);
        }
    }
    if (groups != NULL) {
        for (i = 0U; i < group_count; ++i) {
            free(groups[i].data);
            free(groups[i].slots);
            free(groups[i].atoms);
            free(groups[i].unique);
        }
    }
    free(maps);
    free(groups);
    free(has_writes);
    free(unsafe_target);
    return ok;
}
