/* MiniLD's append-only ET_REL input graph owner.
 *
 * An admitted object's file ID, section IDs and symbol IDs remain stable
 * across COMDAT selection, name-based work-section merging, GC, scripts and
 * relaxation. Mutable work-section placement and sorting belong exclusively
 * to the WorkSections owner. Source bytes and symbol values never change
 * after admission.
 */

static void input_graph_destroy(MiniLdInputGraph *graph) {
    size_t i;

    for (i = 0U; i < graph->file_count; ++i) {
        free(graph->files[i].path);
        free(graph->files[i].section_ids);
    }
    for (i = 0U; i < graph->section_count; ++i) {
        free(graph->sections[i].name);
        free(graph->sections[i].data);
    }
    for (i = 0U; i < graph->symbol_count; ++i) {
        free(graph->symbols[i].name);
    }
    free(graph->files);
    free(graph->sections);
    free(graph->symbols);
    memset(graph, 0, sizeof(*graph));
}

static bool input_graph_record_file(MiniLdInputGraph *graph,
                                    const char *path,
                                    uint32_t elf_flags,
                                    size_t elf_section_count,
                                    size_t *file_out) {
    MiniLdInputFile *entry;
    size_t capacity;
    void *next;

    if (graph->file_count == graph->file_capacity) {
        capacity = graph->file_capacity == 0U
                       ? 16U : graph->file_capacity * 2U;
        if (capacity < graph->file_capacity ||
            capacity > SIZE_MAX / sizeof(*graph->files)) {
            return false;
        }
        next = realloc(graph->files, capacity * sizeof(*graph->files));
        if (next == NULL) {
            return false;
        }
        graph->files = next;
        graph->file_capacity = capacity;
    }
    entry = &graph->files[graph->file_count];
    memset(entry, 0, sizeof(*entry));
    entry->path = minild_strdup(path);
    if (entry->path == NULL) {
        return false;
    }
    if (elf_section_count > SIZE_MAX / sizeof(*entry->section_ids)) {
        free(entry->path);
        entry->path = NULL;
        return false;
    }
    entry->section_ids = malloc(elf_section_count * sizeof(*entry->section_ids));
    if (entry->section_ids == NULL) {
        free(entry->path);
        entry->path = NULL;
        return false;
    }
    for (size_t i = 0U; i < elf_section_count; ++i) {
        entry->section_ids[i] = SIZE_MAX;
    }
    entry->section_slot_count = elf_section_count;
    entry->first_section = graph->section_count;
    entry->first_symbol = graph->symbol_count;
    entry->elf_flags = elf_flags;
    *file_out = graph->file_count++;
    return true;
}

static bool input_graph_record_section(MiniLdInputGraph *graph,
                                       size_t file_index,
                                       size_t elf_index,
                                       const char *name,
                                       const MiniElfSection *header,
                                       const unsigned char *bytes,
                                       size_t data_size,
                                       size_t *input_id_out) {
    MiniLdInputSection *entry;
    size_t capacity;
    void *next;

    if (file_index >= graph->file_count ||
        elf_index >= graph->files[file_index].section_slot_count ||
        header->size > SIZE_MAX ||
        (header->type != SHT_NOBITS && data_size != (size_t)header->size)) {
        return false;
    }
    if (graph->section_count == graph->section_capacity) {
        capacity = graph->section_capacity == 0U
                       ? 64U : graph->section_capacity * 2U;
        if (capacity < graph->section_capacity ||
            capacity > SIZE_MAX / sizeof(*graph->sections)) {
            return false;
        }
        next = realloc(graph->sections,
                       capacity * sizeof(*graph->sections));
        if (next == NULL) {
            return false;
        }
        graph->sections = next;
        graph->section_capacity = capacity;
    }
    entry = &graph->sections[graph->section_count];
    memset(entry, 0, sizeof(*entry));
    entry->name = minild_strdup(name);
    if (entry->name == NULL) {
        return false;
    }
    if (header->type != SHT_NOBITS && data_size != 0U) {
        entry->data = malloc(data_size);
        if (entry->data == NULL) {
            free(entry->name);
            entry->name = NULL;
            return false;
        }
        memcpy(entry->data, bytes, data_size);
    }
    entry->file = file_index;
    entry->elf_index = elf_index;
    entry->type = header->type;
    entry->flags = header->flags;
    entry->align = header->alignment == 0U ? 1U : header->alignment;
    entry->entsize = header->entry_size;
    entry->link = header->link;
    entry->info = header->info;
    entry->size = (size_t)header->size;
    entry->group_signature_symbol = SIZE_MAX;
    entry->link_order_input = SIZE_MAX;
    entry->group_input = SIZE_MAX;
    *input_id_out = graph->section_count++;
    ++graph->files[file_index].section_count;
    graph->files[file_index].section_ids[elf_index] = *input_id_out;
    return true;
}

static bool input_graph_record_symbol(MiniLdInputGraph *graph,
                                      FILE *diagnostics,
                                      size_t file,
                                      size_t elf_index,
                                      const char *name,
                                      const MiniElfSymbol *raw,
                                      size_t *source_out) {
    MiniLdInputSymbol *record;

    if (graph->symbol_count == graph->symbol_capacity) {
        size_t capacity = graph->symbol_capacity == 0U ? 64U :
                          graph->symbol_capacity * 2U;
        MiniLdInputSymbol *next;
        if (capacity < graph->symbol_capacity ||
            capacity > SIZE_MAX / sizeof(*graph->symbols)) {
            goto oom;
        }
        next = realloc(graph->symbols,
                       capacity * sizeof(*graph->symbols));
        if (next == NULL) {
            goto oom;
        }
        graph->symbols = next;
        graph->symbol_capacity = capacity;
    }
    record = &graph->symbols[graph->symbol_count];
    memset(record, 0, sizeof(*record));
    record->name = minild_strdup(name);
    if (record->name == NULL) {
        goto oom;
    }
    record->file = file;
    record->elf_index = elf_index;
    record->elf_section = raw->section_index;
    record->input_section = SIZE_MAX;
    record->value = raw->value;
    record->size = raw->size;
    record->info = raw->info;
    record->other = raw->other;
    record->canonical_symbol = SIZE_MAX;
    *source_out = graph->symbol_count++;
    ++graph->files[file].symbol_count;
    return true;

oom:
    fprintf(diagnostics, "minic-ld: out-of-memory:input-symbols\n");
    return false;
}
