/* Stable canonical-symbol owner for MiniLD.
 *
 * The input owner retains every original ET_REL candidate and its immutable
 * source identity.  This resolver owns the append-only canonical ID space,
 * its global-name hash and the current winner.  Scripts and layout may edit
 * a canonical entry, but never reorder it or modify an input record.
 *
 * Included after the shared hash helpers and MiniLdSymbolResolver types in
 * linker_core.inc; this intentionally remains private to the LD TU.
 */

static void symbol_resolver_destroy(MiniLdSymbolResolver *resolver) {
    size_t i;

    for (i = 0U; i < resolver->count; ++i) {
        free(resolver->entries[i].name);
    }
    free(resolver->entries);
    free(resolver->global_slots);
    memset(resolver, 0, sizeof(*resolver));
}

static bool symbol_resolver_ensure_capacity(MiniLdSymbolResolver *resolver) {
    MiniLdResolvedSymbol *next;
    size_t capacity;

    if (resolver->count < resolver->capacity) {
        return true;
    }
    capacity = resolver->capacity == 0U ? 64U : resolver->capacity * 2U;
    if (capacity < resolver->capacity ||
        capacity > SIZE_MAX / sizeof(*resolver->entries)) {
        return false;
    }
    next = realloc(resolver->entries, capacity * sizeof(*resolver->entries));
    if (next == NULL) {
        return false;
    }
    resolver->entries = next;
    resolver->capacity = capacity;
    return true;
}

static bool symbol_resolver_rebuild_globals(MiniLdSymbolResolver *resolver,
                                            size_t capacity) {
    MiniLdIndexSlot *slots;
    size_t i;

    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (i = 0U; i < resolver->count; ++i) {
        if (ELF64_ST_BIND(resolver->entries[i].info) != STB_LOCAL) {
            uint64_t hash = hash_text(resolver->entries[i].name);
            insert_index_slot(slots, capacity, hash, i);
        }
    }
    free(resolver->global_slots);
    resolver->global_slots = slots;
    resolver->global_capacity = capacity;
    return true;
}

static bool symbol_resolver_ensure_global_capacity(
    MiniLdSymbolResolver *resolver) {
    size_t capacity = resolver->global_capacity;

    if (capacity != 0U &&
        (resolver->global_count + 1U) * 10U < capacity * 7U) {
        return true;
    }
    if (capacity == 0U) {
        capacity = 256U;
    } else {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    return symbol_resolver_rebuild_globals(resolver, capacity);
}

static bool symbol_resolver_add_local(MiniLdSymbolResolver *resolver,
                                      FILE *diagnostics,
                                      const char *name,
                                      int section,
                                      uint64_t value,
                                      uint64_t size,
                                      unsigned char info,
                                      unsigned char other,
                                      size_t source_symbol,
                                      size_t *symbol_out) {
    MiniLdResolvedSymbol *symbol;

    if (!symbol_resolver_ensure_capacity(resolver)) {
        fprintf(diagnostics, "minic-ld: out-of-memory:symbols\n");
        return false;
    }
    symbol = &resolver->entries[resolver->count];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = minild_strdup(name);
    if (symbol->name == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:symbol-name\n");
        return false;
    }
    symbol->section = section;
    symbol->value = value;
    symbol->size = size;
    symbol->info = info;
    symbol->other = other;
    symbol->source_symbol = source_symbol;
    *symbol_out = resolver->count++;
    return true;
}

static bool symbol_resolver_is_defined(const MiniLdResolvedSymbol *symbol) {
    return symbol->section != MINILD_SECTION_UNDEF;
}

static size_t symbol_resolver_lookup(const MiniLdSymbolResolver *resolver,
                                     const char *name) {
    uint64_t hash = hash_text(name);
    size_t position;

    if (resolver->global_capacity == 0U) {
        return SIZE_MAX;
    }
    position = (size_t)hash & (resolver->global_capacity - 1U);
    while (resolver->global_slots[position].index_plus_one != 0U) {
        const MiniLdIndexSlot *slot = &resolver->global_slots[position];
        size_t index = slot->index_plus_one - 1U;

        if (slot->hash == hash &&
            strcmp(resolver->entries[index].name, name) == 0) {
            return index;
        }
        position = (position + 1U) & (resolver->global_capacity - 1U);
    }
    return SIZE_MAX;
}

static bool symbol_resolver_merge_global(MiniLdSymbolResolver *resolver,
                                         FILE *diagnostics,
                                         const char *name,
                                         int section,
                                         uint64_t value,
                                         uint64_t size,
                                         unsigned char info,
                                         unsigned char other,
                                         size_t source_symbol,
                                         size_t *symbol_out) {
    size_t existing_index = symbol_resolver_lookup(resolver, name);
    unsigned new_bind = ELF64_ST_BIND(info);

    if (existing_index == SIZE_MAX) {
        size_t index;
        uint64_t hash = hash_text(name);

        if (!symbol_resolver_ensure_global_capacity(resolver) ||
            !symbol_resolver_add_local(resolver,
                                       diagnostics,
                                       name,
                                       section,
                                       value,
                                       size,
                                       info,
                                       other,
                                       source_symbol,
                                       &index)) {
            return false;
        }
        insert_index_slot(resolver->global_slots,
                          resolver->global_capacity,
                          hash,
                          index);
        ++resolver->global_count;
        *symbol_out = index;
        return true;
    }

    {
        MiniLdResolvedSymbol *existing = &resolver->entries[existing_index];
        unsigned old_bind = ELF64_ST_BIND(existing->info);
        bool old_defined = symbol_resolver_is_defined(existing);
        bool new_defined = section != MINILD_SECTION_UNDEF;

        if (!old_defined && new_defined) {
            existing->section = section;
            existing->value = value;
            existing->size = size;
            existing->info = info;
            existing->other = other;
            existing->source_symbol = source_symbol;
        } else if (old_defined && new_defined) {
            if (old_bind == STB_WEAK && new_bind != STB_WEAK) {
                existing->section = section;
                existing->value = value;
                existing->size = size;
                existing->info = info;
                existing->other = other;
                existing->source_symbol = source_symbol;
            } else if (old_bind != STB_WEAK && new_bind != STB_WEAK) {
                fprintf(diagnostics, "minic-ld: multiple-definition:%s\n", name);
                return false;
            }
        } else if (!old_defined && !new_defined &&
                   old_bind == STB_WEAK && new_bind != STB_WEAK) {
            existing->info = info;
            existing->other = other;
            existing->source_symbol = source_symbol;
        }
    }
    *symbol_out = existing_index;
    return true;
}

/* Lazy archive extraction asks about unmet nonweak references only.  It does
 * not own or modify the global hash or the selected canonical definition. */
static bool symbol_resolver_needs_archive(const MiniLdSymbolResolver *resolver,
                                          const char *name) {
    size_t index = symbol_resolver_lookup(resolver, name);

    return index != SIZE_MAX &&
           !symbol_resolver_is_defined(&resolver->entries[index]) &&
           ELF64_ST_BIND(resolver->entries[index].info) != STB_WEAK;
}

/* A linker-script assignment intentionally overrides a previous canonical
 * value; it must not go through ELF strong-vs-strong candidate selection. */
static bool symbol_resolver_assign_absolute(MiniLdSymbolResolver *resolver,
                                             FILE *diagnostics,
                                             const char *name,
                                             uint64_t value) {
    size_t index = symbol_resolver_lookup(resolver, name);

    if (index != SIZE_MAX) {
        MiniLdResolvedSymbol *symbol = &resolver->entries[index];
        symbol->section = MINILD_SECTION_ABS;
        symbol->value = value;
        symbol->size = 0U;
        symbol->info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        symbol->other = STV_DEFAULT;
        symbol->source_symbol = SIZE_MAX;
        return true;
    }
    return symbol_resolver_merge_global(resolver, diagnostics, name,
                                        MINILD_SECTION_ABS, value, 0U,
                                        ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                                        STV_DEFAULT, SIZE_MAX, &index);
}

/* PROVIDE and linker-synthesized array boundaries only fill missing or
 * undefined symbols; unlike script assignment they never override a value. */
static bool symbol_resolver_provide_absolute(MiniLdSymbolResolver *resolver,
                                              FILE *diagnostics,
                                              const char *name,
                                              uint64_t value) {
    size_t index = symbol_resolver_lookup(resolver, name);

    if (index != SIZE_MAX &&
        symbol_resolver_is_defined(&resolver->entries[index])) {
        return true;
    }
    return symbol_resolver_merge_global(resolver, diagnostics, name,
                                        MINILD_SECTION_ABS, value, 0U,
                                        ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                                        STV_DEFAULT, SIZE_MAX, &index);
}
