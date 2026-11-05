static bool process_whole_archive(MiniLdState *state, const char *path) {
    MiniLdArchive archive = {0};
    size_t extracted;
    bool ok = archive_open(&archive, path, state->diagnostics) &&
              archive_extract(state, &archive, true, &extracted);

    archive_destroy(&archive);
    return ok;
}

static bool process_group_archive(MiniLdState *state, const char *path) {
    MiniLdArchive archive = {0};
    size_t extracted;
    bool ok = archive_open(&archive, path, state->diagnostics) &&
              archive_extract(state, &archive, false, &extracted);

    archive_destroy(&archive);
    return ok;
}

static bool process_object_or_archive(MiniLdState *state,
                                      const char *path) {
    int kind = archive_file_kind(path, state->diagnostics);

    if (kind < 0) {
        return false;
    }
    if (kind == 1 || kind == 2) {
        return process_group_archive(state, path);
    }
    return process_input(state, path);
}


static bool process_group_sequence(MiniLdState *state,
                                   const MiniLdInput *inputs,
                                   size_t input_count) {
    typedef struct MiniLdGroupInput {
        MiniLdArchive archive;
        bool is_archive;
    } MiniLdGroupInput;
    MiniLdGroupInput *entries;
    bool first_pass = true;
    bool changed;
    size_t i;
    bool ok = false;

    entries = calloc(input_count == 0U ? 1U : input_count, sizeof(*entries));
    if (entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:group-inputs\n");
        return false;
    }
    for (i = 0U; i < input_count; ++i) {
        int kind = archive_file_kind(inputs[i].path, state->diagnostics);

        if (kind < 0) {
            goto done;
        }
        entries[i].is_archive = kind == MINILD_ARCHIVE_THIN ||
                                kind == MINILD_ARCHIVE_REGULAR;
        if (entries[i].is_archive &&
            !archive_open_kind(&entries[i].archive, inputs[i].path, kind,
                               state->diagnostics)) {
            goto done;
        }
    }

    do {
        changed = false;

        for (i = 0U; i < input_count; ++i) {
            if (entries[i].is_archive) {
                size_t extracted = 0U;

                if (!archive_extract(state, &entries[i].archive,
                                     inputs[i].kind ==
                                         MINILD_INPUT_GROUP_WHOLE_ARCHIVE,
                                     &extracted)) {
                    goto done;
                }
                changed = changed || extracted != 0U;
            } else if (first_pass) {
                if (!process_input(state, inputs[i].path)) {
                    goto done;
                }
                /* A later plain object can create a need for an earlier
                 * archive even if no archive member was selected this pass. */
                changed = true;
            }
        }
        first_pass = false;
    } while (changed);

    ok = true;

done:
    for (i = 0U; i < input_count; ++i) {
        archive_destroy(&entries[i].archive);
    }
    free(entries);
    return ok;
}

static bool process_input_sequence(MiniLdState *state,
                                   const MiniLdInput *inputs,
                                   size_t input_count) {
    size_t i = 0U;

    while (i < input_count) {
        bool input_ok;

        if (inputs[i].kind == MINILD_INPUT_GROUP_BREAK) {
            ++i;
            continue;
        }
        if (inputs[i].kind == MINILD_INPUT_GROUP_ARCHIVE ||
            inputs[i].kind == MINILD_INPUT_GROUP_WHOLE_ARCHIVE) {
            size_t end = i + 1U;

            while (end < input_count &&
                   (inputs[end].kind == MINILD_INPUT_GROUP_ARCHIVE ||
                    inputs[end].kind == MINILD_INPUT_GROUP_WHOLE_ARCHIVE)) {
                ++end;
            }
            if (!process_group_sequence(state, inputs + i, end - i)) {
                return false;
            }
            i = end;
            continue;
        }

        switch (inputs[i].kind) {
        case MINILD_INPUT_OBJECT:
            input_ok = process_object_or_archive(state, inputs[i].path);
            break;
        case MINILD_INPUT_WHOLE_ARCHIVE:
            input_ok = process_whole_archive(state, inputs[i].path);
            break;
        case MINILD_INPUT_ARCHIVE:
            input_ok = process_group_archive(state, inputs[i].path);
            break;
        default:
            fprintf(state->diagnostics,
                    "minic-ld: invalid-input-kind:%s\n",
                    inputs[i].path);
            input_ok = false;
            break;
        }
        if (!input_ok) {
            return false;
        }
        ++i;
    }
    return true;
}
