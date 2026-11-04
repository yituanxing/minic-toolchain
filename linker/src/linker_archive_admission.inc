typedef enum MiniLdArchiveKind {
    MINILD_ARCHIVE_THIN = 1,
    MINILD_ARCHIVE_REGULAR = 2
} MiniLdArchiveKind;

typedef struct MiniLdArchiveMember {
    char *name;
    /* Thin members own an external path; regular members borrow archive data. */
    char *path;
    size_t data_offset;
    size_t data_size;
    bool selected;
} MiniLdArchiveMember;

typedef struct MiniLdArchive {
    MiniLdArchiveKind kind;
    unsigned char *data;
    size_t size;
    MiniLdArchiveMember *members;
    size_t count;
    size_t capacity;
} MiniLdArchive;

static char *archive_directory(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t length;
    char *result;

    if (slash == NULL) {
        return minild_strdup(".");
    }
    if (slash == path) {
        return minild_strdup("/");
    }
    length = (size_t)(slash - path);
    result = malloc(length + 1U);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, path, length);
    result[length] = '\0';
    return result;
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_size;
    size_t name_size;
    bool needs_slash;
    char *result;

    if (name[0] == '/') {
        return minild_strdup(name);
    }
    directory_size = strlen(directory);
    name_size = strlen(name);
    needs_slash = directory_size != 0U && directory[directory_size - 1U] != '/';
    if (directory_size > SIZE_MAX - name_size - (needs_slash ? 2U : 1U)) {
        return NULL;
    }
    result = malloc(directory_size + (needs_slash ? 1U : 0U) + name_size + 1U);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, directory, directory_size);
    if (needs_slash) {
        result[directory_size++] = '/';
    }
    memcpy(result + directory_size, name, name_size + 1U);
    return result;
}

static void archive_destroy(MiniLdArchive *archive) {
    size_t i;

    for (i = 0U; i < archive->count; ++i) {
        free(archive->members[i].name);
        free(archive->members[i].path);
    }
    free(archive->members);
    free(archive->data);
    memset(archive, 0, sizeof(*archive));
}

static bool archive_member_append(MiniLdArchive *archive,
                                  char *name,
                                  char *path,
                                  size_t offset,
                                  size_t size) {
    MiniLdArchiveMember *next;
    size_t capacity;

    if (archive->count == archive->capacity) {
        capacity = archive->capacity == 0U ? 64U : archive->capacity * 2U;
        if (capacity < archive->capacity ||
            capacity > SIZE_MAX / sizeof(*archive->members)) {
            return false;
        }
        next = realloc(archive->members, capacity * sizeof(*archive->members));
        if (next == NULL) {
            return false;
        }
        archive->members = next;
        archive->capacity = capacity;
    }
    archive->members[archive->count++] = (MiniLdArchiveMember){
        .name = name,
        .path = path,
        .data_offset = offset,
        .data_size = size,
        .selected = false
    };
    return true;
}

static bool parse_archive_decimal(const unsigned char *field,
                                  size_t width,
                                  uint64_t *value_out) {
    uint64_t value = 0U;
    size_t i = 0U;
    bool saw_digit = false;

    while (i < width && field[i] == ' ') {
        ++i;
    }
    for (; i < width && field[i] != ' '; ++i) {
        unsigned digit;

        if (field[i] < '0' || field[i] > '9') {
            return false;
        }
        digit = (unsigned)(field[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        saw_digit = true;
    }
    while (i < width) {
        if (field[i] != ' ') {
            return false;
        }
        ++i;
    }
    if (!saw_digit) {
        value = 0U;
    }
    *value_out = value;
    return true;
}

static bool parse_archive_name_field(const unsigned char *header,
                                     char field[17]) {
    size_t length = 16U;

    memcpy(field, header, 16U);
    field[16] = '\0';
    while (length != 0U && field[length - 1U] == ' ') {
        field[--length] = '\0';
    }
    return length != 0U;
}

static bool decode_archive_member_name(const unsigned char *long_names,
                                       size_t long_names_size,
                                       const char field[17],
                                       char **name_out) {
    char *name;
    size_t length;

    if (field[0] == '/' && field[1] >= '0' && field[1] <= '9') {
        uint64_t offset = 0U;
        const char *p = field + 1;
        size_t end;

        while (*p >= '0' && *p <= '9') {
            unsigned digit = (unsigned)(*p - '0');
            if (offset > (UINT64_MAX - digit) / 10U) {
                return false;
            }
            offset = offset * 10U + digit;
            ++p;
        }
        while (*p == ' ') {
            ++p;
        }
        if (*p == '/') {
            ++p;
        }
        while (*p == ' ') {
            ++p;
        }
        if (*p != '\0' || offset > SIZE_MAX ||
            (size_t)offset >= long_names_size) {
            return false;
        }
        end = (size_t)offset;
        while (end < long_names_size && long_names[end] != '\n') {
            ++end;
        }
        if (end == long_names_size || end == (size_t)offset ||
            long_names[end - 1U] != '/') {
            return false;
        }
        --end;
        length = end - (size_t)offset;
        name = malloc(length + 1U);
        if (name == NULL) {
            return false;
        }
        memcpy(name, long_names + (size_t)offset, length);
        name[length] = '\0';
        *name_out = name;
        return true;
    }

    if (strncmp(field, "#1/", 3U) == 0) {
        return false;
    }
    {
        const char *slash = strchr(field, '/');
        length = slash == NULL ? strlen(field) : (size_t)(slash - field);
    }
    name = malloc(length + 1U);
    if (name == NULL) {
        return false;
    }
    memcpy(name, field, length);
    name[length] = '\0';
    *name_out = name;
    return true;
}

static bool read_thin_archive_members(const char *archive_path,
                                      MiniLdArchive *archive,
                                      FILE *diagnostics) {
    unsigned char *data = NULL;
    size_t size = 0U;
    size_t cursor = 8U;
    const unsigned char *long_names = NULL;
    size_t long_names_size = 0U;
    char *directory = NULL;
    bool ok = false;

    if (!read_file(archive_path, &data, &size, diagnostics)) {
        return false;
    }
    if (size < 8U || memcmp(data, "!<thin>\n", 8U) != 0) {
        fprintf(diagnostics,
                "minic-ld: A1 archive input must be GNU thin archive:%s\n",
                archive_path);
        goto done;
    }
    directory = archive_directory(archive_path);
    if (directory == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:archive-directory\n");
        goto done;
    }

    while (cursor < size) {
        const unsigned char *header;
        char field[17];
        uint64_t member_size;
        bool embedded;

        if (!range_ok(cursor, 60U, size)) {
            fprintf(diagnostics, "minic-ld: truncated-archive-header:%s\n", archive_path);
            goto done;
        }
        header = data + cursor;
        if (header[58] != (unsigned char)0x60 || header[59] != '\n' ||
            !parse_archive_name_field(header, field) ||
            !parse_archive_decimal(header + 48U, 10U, &member_size) ||
            member_size > SIZE_MAX) {
            fprintf(diagnostics, "minic-ld: invalid-archive-header:%s\n", archive_path);
            goto done;
        }
        cursor += 60U;
        embedded = strcmp(field, "/") == 0 || strcmp(field, "//") == 0 ||
                   strcmp(field, "/SYM64/") == 0;

        if (embedded && !range_ok(cursor, (size_t)member_size, size)) {
            fprintf(diagnostics, "minic-ld: truncated-archive-special:%s\n", archive_path);
            goto done;
        }

        if (strcmp(field, "//") == 0) {
            long_names = data + cursor;
            long_names_size = (size_t)member_size;
        } else if (strcmp(field, "/") != 0 && strcmp(field, "/SYM64/") != 0) {
            char *name = NULL;
            char *path = NULL;

            if (!decode_archive_member_name(long_names,
                                            long_names_size,
                                            field,
                                            &name)) {
                fprintf(diagnostics,
                        "minic-ld: unsupported-archive-member-name:%s:%s\n",
                        archive_path,
                        field);
                goto done;
            }
            path = join_path(directory, name);
            if (path == NULL ||
                !archive_member_append(archive, name, path, 0U, 0U)) {
                free(name);
                free(path);
                fprintf(diagnostics, "minic-ld: out-of-memory:archive-members\n");
                goto done;
            }
        }

        if (embedded) {
            cursor += (size_t)member_size;
            if ((member_size & 1U) != 0U) {
                if (!range_ok(cursor, 1U, size)) {
                    fprintf(diagnostics,
                            "minic-ld: truncated-archive-padding:%s\n",
                            archive_path);
                    goto done;
                }
                ++cursor;
            }
        }
    }

    ok = true;

done:
    free(directory);
    free(data);
    return ok;
}

static bool state_has_unresolved_nonweak(const MiniLdState *state,
                                         const char *name) {
    return symbol_resolver_needs_archive(&state->resolver, name);
}

static bool object_data_defines_needed_symbol(MiniLdState *state,
                                              const unsigned char *data,
                                              size_t size,
                                              const char *path,
                                              bool *needed_out) {
    MiniElfView elf;
    MiniElfSection symtab;
    MiniElfSection strtab;
    size_t symtab_index = SIZE_MAX;
    size_t i;
    bool ok = false;

    *needed_out = false;
    if (!minielf_open(&elf, data, size) ||
        !minild_target_accept_elf(state, &elf, ET_REL)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-archive-object:%s\n",
                path);
        goto done;
    }

    for (i = 1U; i < elf.section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(&elf, i, &section)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-archive-object:%s\n",
                    path);
            goto done;
        }
        if (section.type == SHT_SYMTAB) {
            symtab = section;
            symtab_index = i;
            break;
        }
    }
    if (symtab_index == SIZE_MAX) {
        /*
         * A regular archive may contain ET_REL members with no symbol table.
         * Such a member cannot satisfy a named unresolved global, so lazy
         * archive selection must simply leave it unselected.
         */
        ok = true;
        goto done;
    }
    if (symtab.link >= elf.section_count ||
        symtab.entry_size != (state->target->elf_class == ELFCLASS32
                                  ? sizeof(Elf32_Sym) : sizeof(Elf64_Sym)) ||
        !minielf_section(&elf, symtab.link, &strtab) ||
        strtab.type != SHT_STRTAB) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-archive-object-symtab:%s\n",
                path);
        goto done;
    }

    {
        size_t count = minielf_symbol_count(&elf, &symtab);

        if (count == 0U && symtab.size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-archive-object-symtab:%s\n",
                    path);
            goto done;
        }
        for (i = 1U; i < count; ++i) {
            MiniElfSymbol symbol;
            const char *name;
            unsigned bind;

            if (!minielf_symbol(&elf, symtab_index, i, &symbol)) {
                fprintf(state->diagnostics,
                        "minic-ld: truncated-archive-object-symbol:%s\n",
                        path);
                goto done;
            }
            bind = minielf_symbol_bind(symbol.info);
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                symbol.section_index == SHN_UNDEF || symbol.name == 0U) {
                continue;
            }
            if (!minielf_string(&elf, symtab.link, symbol.name, &name)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-archive-object-symbol-name:%s\n",
                        path);
                goto done;
            }
            if (state_has_unresolved_nonweak(state, name)) {
                *needed_out = true;
                break;
            }
        }
    }
    ok = true;

done:
    return ok;
}

static bool read_regular_archive(const char *archive_path,
                                 MiniLdArchive *archive,
                                 FILE *diagnostics) {
    size_t cursor = 8U;
    const unsigned char *long_names = NULL;
    size_t long_names_size = 0U;
    bool ok = false;

    if (!read_file(archive_path, &archive->data, &archive->size, diagnostics)) {
        return false;
    }
    if (archive->size < 8U ||
        memcmp(archive->data, "!<arch>\n", 8U) != 0) {
        fprintf(diagnostics,
                "minic-ld: A3 archive input must be GNU regular archive:%s\n",
                archive_path);
        goto done;
    }

    while (cursor < archive->size) {
        const unsigned char *header;
        char field[17];
        uint64_t member_size;
        size_t payload_offset;

        if (!range_ok(cursor, 60U, archive->size)) {
            fprintf(diagnostics,
                    "minic-ld: truncated-regular-archive-header:%s\n",
                    archive_path);
            goto done;
        }
        header = archive->data + cursor;
        if (header[58] != (unsigned char)0x60 || header[59] != '\n' ||
            !parse_archive_name_field(header, field) ||
            !parse_archive_decimal(header + 48U, 10U, &member_size) ||
            member_size > SIZE_MAX) {
            fprintf(diagnostics,
                    "minic-ld: invalid-regular-archive-header:%s\n",
                    archive_path);
            goto done;
        }
        cursor += 60U;
        payload_offset = cursor;
        if (!range_ok(payload_offset, (size_t)member_size, archive->size)) {
            fprintf(diagnostics,
                    "minic-ld: truncated-regular-archive-member:%s:%s\n",
                    archive_path,
                    field);
            goto done;
        }

        if (strcmp(field, "//") == 0) {
            long_names = archive->data + payload_offset;
            long_names_size = (size_t)member_size;
        } else if (strcmp(field, "/") != 0 &&
                   strcmp(field, "/SYM64/") != 0) {
            char *name = NULL;

            if (!decode_archive_member_name(long_names,
                                            long_names_size,
                                            field,
                                            &name)) {
                fprintf(diagnostics,
                        "minic-ld: unsupported-regular-member-name:%s:%s\n",
                        archive_path,
                        field);
                goto done;
            }
            if (!archive_member_append(archive,
                                       name,
                                       NULL,
                                       payload_offset,
                                       (size_t)member_size)) {
                free(name);
                fprintf(diagnostics,
                        "minic-ld: out-of-memory:regular-archive-members\n");
                goto done;
            }
        }

        cursor += (size_t)member_size;
        if ((member_size & 1U) != 0U) {
            if (!range_ok(cursor, 1U, archive->size)) {
                fprintf(diagnostics,
                        "minic-ld: truncated-regular-archive-padding:%s\n",
                        archive_path);
                goto done;
            }
            ++cursor;
        }
    }

    ok = true;

done:
    return ok;
}

static int archive_file_kind(const char *path, FILE *diagnostics) {
    FILE *file;
    unsigned char magic[8];
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(diagnostics,
                "minic-ld: cannot-open:%s:%s\n",
                path,
                strerror(errno));
        return -1;
    }
    got = fread(magic, 1U, sizeof(magic), file);
    if (fclose(file) != 0) {
        fprintf(diagnostics, "minic-ld: cannot-close:%s\n", path);
        return -1;
    }
    if (got != sizeof(magic)) {
        return 0;
    }
    if (memcmp(magic, "!<thin>\n", 8U) == 0) {
        return 1;
    }
    if (memcmp(magic, "!<arch>\n", 8U) == 0) {
        return 2;
    }
    return 0;
}

/* Archive owns regular-member bytes and thin-member paths.  The extraction
 * policy never needs to know how a member's bytes were obtained. */
typedef struct MiniLdArchiveMemberView {
    const unsigned char *bytes;
    size_t size;
    unsigned char *owned_bytes;
} MiniLdArchiveMemberView;

static bool archive_open_kind(MiniLdArchive *archive,
                              const char *path,
                              int kind,
                              FILE *diagnostics) {
    bool ok;

    if (kind != MINILD_ARCHIVE_THIN && kind != MINILD_ARCHIVE_REGULAR) {
        fprintf(diagnostics, "minic-ld: expected-archive:%s\n", path);
        return false;
    }
    archive->kind = (MiniLdArchiveKind)kind;
    if (archive->kind == MINILD_ARCHIVE_THIN) {
        ok = read_thin_archive_members(path, archive, diagnostics);
    } else {
        ok = read_regular_archive(path, archive, diagnostics);
    }
    if (!ok) {
        archive_destroy(archive);
    }
    return ok;
}

static bool archive_open(MiniLdArchive *archive,
                         const char *path,
                         FILE *diagnostics) {
    int kind = archive_file_kind(path, diagnostics);

    return kind >= 0 && archive_open_kind(archive, path, kind, diagnostics);
}

static const char *archive_member_path(const MiniLdArchiveMember *member) {
    return member->path == NULL ? member->name : member->path;
}

static bool archive_member_view(MiniLdState *state,
                                const MiniLdArchive *archive,
                                const MiniLdArchiveMember *member,
                                MiniLdArchiveMemberView *view) {
    memset(view, 0, sizeof(*view));
    if (archive->kind == MINILD_ARCHIVE_REGULAR) {
        view->bytes = archive->data + member->data_offset;
        view->size = member->data_size;
        return true;
    }
    if (!read_file(member->path, &view->owned_bytes, &view->size,
                   state->diagnostics)) {
        return false;
    }
    view->bytes = view->owned_bytes;
    return true;
}

static bool archive_member_process(MiniLdState *state,
                                   const MiniLdArchive *archive,
                                   const MiniLdArchiveMember *member,
                                   const MiniLdArchiveMemberView *view) {
    const char *path = archive_member_path(member);
    MiniElfView elf;

    /* Preserve the existing distinction: only thin whole-archive members
     * were allowed to dispatch ET_DYN through process_input(). */
    if (archive->kind == MINILD_ARCHIVE_THIN &&
        minielf_open(&elf, view->bytes, view->size) && elf.type == ET_DYN) {
        if (!state->allow_dso_inputs) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-input-requires-dynamic-link:%s\n",
                    path);
            return false;
        }
        return process_dso_input_data(state, view->bytes, view->size, path);
    }
    return process_input_data(state, view->bytes, view->size, path);
}

/* One fixed-point member selector for both archive encodings.  An extracted
 * member may introduce a new undefined symbol satisfied by an earlier member.
 * selected persists across outer --start-group passes to avoid reingestion. */
static bool archive_extract(MiniLdState *state,
                            MiniLdArchive *archive,
                            bool whole,
                            size_t *extracted_out) {
    bool changed;
    size_t extracted = 0U;
    size_t i;

    do {
        changed = false;
        for (i = 0U; i < archive->count; ++i) {
            MiniLdArchiveMember *member = &archive->members[i];
            MiniLdArchiveMemberView view;
            bool needed = whole;
            bool ok;

            if (member->selected) {
                continue;
            }
            if (!archive_member_view(state, archive, member, &view)) {
                return false;
            }
            if (!whole) {
                ok = object_data_defines_needed_symbol(
                    state, view.bytes, view.size,
                    archive_member_path(member), &needed);
                if (!ok) {
                    free(view.owned_bytes);
                    return false;
                }
            }
            ok = !needed || archive_member_process(state, archive, member, &view);
            free(view.owned_bytes);
            if (!ok) {
                return false;
            }
            if (!needed) {
                continue;
            }
            member->selected = true;
            ++extracted;
            changed = true;
        }
    } while (!whole && changed);
    *extracted_out = extracted;
    return true;
}
