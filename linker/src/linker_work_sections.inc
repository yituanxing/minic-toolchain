/* The mutable work-section owner.  Input sections retain original ELF
 * identities; final-link output sections are projected separately. */

static void work_sections_destroy(MiniLdSectionWorkspace *work) {
    size_t i;

    for (i = 0U; i < work->count; ++i) {
        free(work->sections[i].name);
        free(work->sections[i].data);
    }
    free(work->sections);
    free(work->index);
    free(work->placements);
    memset(work, 0, sizeof(*work));
}

/* Per-input placement is mutable linking state, keyed by the stable source
 * input-section ID. Projection and later permutations both remap one slot. */
static bool work_placement_append(MiniLdSectionWorkspace *work,
                                  size_t work_section) {
    MiniLdInputPlacement *next;
    size_t capacity;

    if (work->placement_count == work->placement_capacity) {
        capacity = work->placement_capacity == 0U ? 64U :
                   work->placement_capacity * 2U;
        if (capacity < work->placement_capacity ||
            capacity > SIZE_MAX / sizeof(*work->placements)) {
            return false;
        }
        next = realloc(work->placements,
                       capacity * sizeof(*work->placements));
        if (next == NULL) {
            return false;
        }
        work->placements = next;
        work->placement_capacity = capacity;
    }
    work->placements[work->placement_count++] = (MiniLdInputPlacement){
        .work_section = work_section
    };
    return true;
}

static bool work_sections_ensure_capacity(MiniLdSectionWorkspace *work) {
    MiniLdSection *next;
    size_t capacity;

    if (work->count < work->capacity) {
        return true;
    }
    capacity = work->capacity == 0U ? 16U : work->capacity * 2U;
    if (capacity < work->capacity ||
        capacity > SIZE_MAX / sizeof(*work->sections)) {
        return false;
    }
    next = realloc(work->sections, capacity * sizeof(*work->sections));
    if (next == NULL) {
        return false;
    }
    work->sections = next;
    work->capacity = capacity;
    return true;
}

static bool section_append_zero(MiniLdSection *section, size_t size) {
    unsigned char *next;
    size_t required;
    size_t capacity;

    if (section->type == SHT_NOBITS) {
        return add_size(section->size, size, &section->size);
    }
    if (!add_size(section->size, size, &required)) {
        return false;
    }
    if (required > section->capacity) {
        capacity = section->capacity == 0U ? 128U : section->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        next = realloc(section->data, capacity);
        if (next == NULL) {
            return false;
        }
        section->data = next;
        section->capacity = capacity;
    }
    if (size != 0U) {
        memset(section->data + section->size, 0, size);
    }
    section->size = required;
    return true;
}

static bool section_append_data(MiniLdSection *section,
                                const unsigned char *data,
                                size_t size) {
    size_t old_size = section->size;

    if (section->type == SHT_NOBITS || !section_append_zero(section, size)) {
        return false;
    }
    if (size != 0U) {
        memcpy(section->data + old_size, data, size);
    }
    return true;
}

/* A distinct -r COMDAT/LINK_ORDER fragment is deliberately absent from this
 * name map.  Rebuilds after a work-section permutation must preserve that
 * semantic; otherwise a later synthetic section may merge into an input. */
static bool work_sections_rebuild_index(MiniLdSectionWorkspace *work,
                                        size_t capacity) {
    MiniLdIndexSlot *slots;
    size_t i;

    if (capacity == 0U || (capacity & (capacity - 1U)) != 0U ||
        work->count >= capacity) {
        return false;
    }
    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < work->count; ++i) {
        if (work->sections[i].indexable) {
            insert_index_slot(slots, capacity,
                              hash_text(work->sections[i].name), i);
        }
    }
    free(work->index);
    work->index = slots;
    work->index_capacity = capacity;
    return true;
}

static bool work_sections_ensure_index_insert(MiniLdSectionWorkspace *work) {
    size_t capacity = work->index_capacity;

    if (capacity != 0U &&
        (work->count + 1U) * 10U < capacity * 7U) {
        return true;
    }
    if (capacity == 0U) {
        capacity = 128U;
    } else {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    return work_sections_rebuild_index(work, capacity);
}

static size_t work_sections_find_index(const MiniLdSectionWorkspace *work,
                                       const char *name,
                                       uint64_t hash) {
    size_t position;

    if (work->index_capacity == 0U) {
        return SIZE_MAX;
    }
    position = (size_t)hash & (work->index_capacity - 1U);
    while (work->index[position].index_plus_one != 0U) {
        const MiniLdIndexSlot *slot = &work->index[position];
        size_t index = slot->index_plus_one - 1U;

        if (slot->hash == hash &&
            strcmp(work->sections[index].name, name) == 0) {
            return index;
        }
        position = (position + 1U) & (work->index_capacity - 1U);
    }
    return SIZE_MAX;
}

static bool work_sections_find_or_add(MiniLdSectionWorkspace *work,
                                      FILE *diagnostics,
                                      const char *name,
                                      uint32_t type,
                                      uint64_t flags,
                                      uint64_t align,
                                      uint64_t entsize,
                                      bool distinct,
                                      size_t *section_out) {
    uint64_t hash = hash_text(name);
    size_t index = distinct ? SIZE_MAX :
                   work_sections_find_index(work, name, hash);
    MiniLdSection *section;

    if (index != SIZE_MAX) {
        section = &work->sections[index];
        if (section->type != type || section->flags != flags ||
            section->entsize != entsize) {
            fprintf(diagnostics, "minic-ld: incompatible-section:%s\n", name);
            return false;
        }
        if (align > section->align) {
            section->align = align;
        }
        *section_out = index;
        return true;
    }
    if (!work_sections_ensure_capacity(work) ||
        (!distinct && !work_sections_ensure_index_insert(work))) {
        fprintf(diagnostics, "minic-ld: out-of-memory:sections\n");
        return false;
    }
    index = work->count;
    section = &work->sections[index];
    memset(section, 0, sizeof(*section));
    section->name = minild_strdup(name);
    if (section->name == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:section-name\n");
        return false;
    }
    section->type = type;
    section->flags = flags;
    section->align = align == 0U ? 1U : align;
    section->entsize = entsize;
    section->group_signature_symbol = SIZE_MAX;
    section->link_order_target = SIZE_MAX;
    section->indexable = !distinct;
    ++work->count;
    if (!distinct) {
        insert_index_slot(work->index, work->index_capacity, hash, index);
    }
    *section_out = index;
    return true;
}
