#include "minielf.h"
#include "miniar.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STB_GNU_UNIQUE
#define STB_GNU_UNIQUE 10
#endif

#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif

typedef enum NmSortMode {
    NM_SORT_NAME,
    NM_SORT_NUMERIC,
    NM_SORT_NONE
} NmSortMode;

typedef struct NmOptions {
    NmSortMode sort_mode;
    bool external_only;
    bool undefined_only;
    bool defined_only;
    bool reverse;
    bool print_file_name;
    bool debug_symbols;
} NmOptions;

typedef struct NmEntry {
    MiniElfSymbol symbol;
    const char *name;
    char type;
    size_t original_index;
} NmEntry;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [options] file...\n"
            "  -n, --numeric-sort       sort symbols numerically by address\n"
            "  -p, --no-sort            preserve symbol-table order\n"
            "  -g, --extern-only        display only external symbols\n"
            "  -u, --undefined-only     display only undefined symbols\n"
            "      --defined-only       display only defined symbols\n"
            "  -r, --reverse-sort       reverse the selected sort order\n"
            "  -A, -o, --print-file-name prefix each symbol with its file\n"
            "  -a, --debug-syms         include debug/special named symbols\n",
            program);
}

static bool read_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(stderr, "minic-nm: %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "minic-nm: %s: cannot determine file size\n", path);
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fprintf(stderr, "minic-nm: %s: out of memory\n", path);
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        fprintf(stderr, "minic-nm: %s: read error\n", path);
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "minic-nm: %s: close error\n", path);
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static char localize_type(char type, unsigned bind) {
    if (bind == STB_LOCAL && type >= 'A' && type <= 'Z') {
        return (char)(type - 'A' + 'a');
    }
    return type;
}

static char classify_symbol(const MiniElfView *view,
                            const MiniElfSymbol *symbol) {
    unsigned bind = minielf_symbol_bind(symbol->info);
    unsigned type = minielf_symbol_type(symbol->info);
    MiniElfSection section;

    if (symbol->section_index == SHN_UNDEF) {
        if (bind == STB_WEAK) {
            return type == STT_OBJECT ? 'v' : 'w';
        }
        return 'U';
    }
    if (bind == STB_GNU_UNIQUE) {
        return 'u';
    }
    if (bind == STB_WEAK) {
        return type == STT_OBJECT ? 'V' : 'W';
    }
    if (symbol->section_index == SHN_ABS) {
        return localize_type('A', bind);
    }
    if (symbol->section_index == SHN_COMMON) {
        return localize_type('C', bind);
    }
    if (type == STT_GNU_IFUNC) {
        return 'i';
    }
    if (symbol->section_index >= view->section_count ||
        !minielf_section(view, symbol->section_index, &section)) {
        return '?';
    }
    if ((section.flags & SHF_EXECINSTR) != 0U) {
        return localize_type('T', bind);
    }
    if (section.type == SHT_NOBITS &&
        (section.flags & SHF_ALLOC) != 0U &&
        (section.flags & SHF_WRITE) != 0U) {
        return localize_type('B', bind);
    }
    if ((section.flags & SHF_ALLOC) != 0U &&
        (section.flags & SHF_WRITE) != 0U) {
        return localize_type('D', bind);
    }
    if ((section.flags & SHF_ALLOC) != 0U) {
        return localize_type('R', bind);
    }
    return localize_type('N', bind);
}

static bool is_mapping_symbol(const char *name) {
    if (name[0] != '    unsigned bind = minielf_symbol_bind(symbol->info);
    unsigned type = minielf_symbol_type(symbol->info);
    bool defined = symbol->section_index != SHN_UNDEF;

    if (name[0] == '\0') {
        return false;
    }
    if (!options->debug_symbols &&
        (type == STT_SECTION || type == STT_FILE ||
         (bind == STB_LOCAL && is_mapping_symbol(name)))) {
        return false;
    }
    if (options->external_only && bind == STB_LOCAL) {
        return false;
    }
    if (options->undefined_only && defined) {
        return false;
    }
    if (options->defined_only && !defined) {
        return false;
    }
    return true;
}

static int compare_name(const void *left, const void *right) {
    const NmEntry *a = left;
    const NmEntry *b = right;
    int result = strcmp(a->name, b->name);

    if (result != 0) {
        return result;
    }
    if (a->symbol.value < b->symbol.value) {
        return -1;
    }
    if (a->symbol.value > b->symbol.value) {
        return 1;
    }
    if (a->original_index < b->original_index) {
        return -1;
    }
    return a->original_index > b->original_index ? 1 : 0;
}

static int compare_numeric(const void *left, const void *right) {
    const NmEntry *a = left;
    const NmEntry *b = right;
    bool a_undefined = a->symbol.section_index == SHN_UNDEF;
    bool b_undefined = b->symbol.section_index == SHN_UNDEF;

    if (a_undefined != b_undefined) {
        return a_undefined ? -1 : 1;
    }
    if (a->symbol.value < b->symbol.value) {
        return -1;
    }
    if (a->symbol.value > b->symbol.value) {
        return 1;
    }
    return compare_name(left, right);
}

static void reverse_entries(NmEntry *entries, size_t count) {
    size_t i;

    for (i = 0U; i < count / 2U; ++i) {
        NmEntry tmp = entries[i];
        entries[i] = entries[count - 1U - i];
        entries[count - 1U - i] = tmp;
    }
}

static bool find_symbol_table(const MiniElfView *view,
                              size_t *index_out,
                              MiniElfSection *section_out) {
    size_t dynamic_index = SIZE_MAX;
    MiniElfSection dynamic_section;
    size_t i;

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(view, i, &section)) {
            return false;
        }
        if (section.type == SHT_SYMTAB) {
            *index_out = i;
            *section_out = section;
            return true;
        }
        if (section.type == SHT_DYNSYM && dynamic_index == SIZE_MAX) {
            dynamic_index = i;
            dynamic_section = section;
        }
    }
    if (dynamic_index != SIZE_MAX) {
        *index_out = dynamic_index;
        *section_out = dynamic_section;
        return true;
    }
    return false;
}

static void print_entry(const MiniElfView *view,
                        const NmOptions *options,
                        const char *path,
                        const NmEntry *entry) {
    int width = view->elf_class == ELFCLASS64 ? 16 : 8;
    bool undefined = entry->symbol.section_index == SHN_UNDEF;

    if (options->print_file_name) {
        printf("%s:", path);
    }
    if (undefined) {
        printf("%*s %c %s\n", width, "", entry->type, entry->name);
    } else {
        printf("%0*llx %c %s\n",
               width,
               (unsigned long long)entry->symbol.value,
               entry->type,
               entry->name);
    }
}

static bool process_elf_data(const unsigned char *data,
                             size_t size,
                             const char *display_name,
                             const NmOptions *options,
                             bool print_header) {
    MiniElfView view;
    MiniElfSection symtab;
    size_t symtab_index;
    size_t symbol_count;
    NmEntry *entries = NULL;
    size_t entry_count = 0U;
    size_t i;
    bool ok = false;

    if (!minielf_open(&view, data, size) ||
        (view.type != ET_REL && view.type != ET_EXEC && view.type != ET_DYN)) {
        fprintf(stderr,
                "minic-nm: %s: unsupported file format\n",
                display_name);
        goto done;
    }
    if (!find_symbol_table(&view, &symtab_index, &symtab)) {
        fprintf(stderr, "minic-nm: %s: no symbols\n", display_name);
        goto done;
    }
    if (symtab.link >= view.section_count) {
        fprintf(stderr,
                "minic-nm: %s: invalid symbol table\n",
                display_name);
        goto done;
    }

    symbol_count = minielf_symbol_count(&view, &symtab);
    if (symbol_count == 0U && symtab.size != 0U) {
        fprintf(stderr,
                "minic-nm: %s: invalid symbol table\n",
                display_name);
        goto done;
    }
    entries = calloc(symbol_count == 0U ? 1U : symbol_count,
                     sizeof(*entries));
    if (entries == NULL) {
        fprintf(stderr, "minic-nm: %s: out of memory\n", display_name);
        goto done;
    }

    for (i = 1U; i < symbol_count; ++i) {
        MiniElfSymbol symbol;
        const char *name;
        NmEntry *entry;

        if (!minielf_symbol(&view, symtab_index, i, &symbol) ||
            !minielf_string(&view, symtab.link, symbol.name, &name)) {
            fprintf(stderr,
                    "minic-nm: %s: invalid symbol entry\n",
                    display_name);
            goto done;
        }
        if (!should_show(options, &symbol, name)) {
            continue;
        }
        entry = &entries[entry_count++];
        entry->symbol = symbol;
        entry->name = name;
        entry->type = classify_symbol(&view, &symbol);
        entry->original_index = i;
    }

    if (options->sort_mode == NM_SORT_NAME) {
        qsort(entries, entry_count, sizeof(*entries), compare_name);
    } else if (options->sort_mode == NM_SORT_NUMERIC) {
        qsort(entries, entry_count, sizeof(*entries), compare_numeric);
    }
    if (options->reverse) {
        reverse_entries(entries, entry_count);
    }

    if (print_header && !options->print_file_name) {
        printf("\n%s:\n", display_name);
    }
    for (i = 0U; i < entry_count; ++i) {
        print_entry(&view, options, display_name, &entries[i]);
    }
    ok = true;

done:
    free(entries);
    return ok;
}

static bool process_elf_path(const char *path,
                             const char *display_name,
                             const NmOptions *options,
                             bool print_header) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool ok;

    if (!read_file(path, &data, &size)) {
        return false;
    }
    ok = process_elf_data(data,
                          size,
                          display_name,
                          options,
                          print_header);
    free(data);
    return ok;
}

typedef struct NmArchiveContext {
    const NmOptions *options;
    bool ok;
} NmArchiveContext;

static bool process_archive_member(const MiniArMemberView *member,
                                   void *context,
                                   FILE *diagnostics) {
    NmArchiveContext *archive = context;
    bool ok;

    (void)diagnostics;
    if (member->data != NULL) {
        ok = process_elf_data(member->data,
                              member->size,
                              member->name,
                              archive->options,
                              true);
    } else if (member->external_path != NULL) {
        ok = process_elf_path(member->external_path,
                              member->name,
                              archive->options,
                              true);
    } else {
        fprintf(stderr,
                "minic-nm: %s: archive member has no payload\n",
                member->name);
        ok = false;
    }
    if (!ok) {
        archive->ok = false;
    }
    return true;
}

static bool process_path(const char *path,
                         const NmOptions *options,
                         bool print_header) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool archive = false;
    bool ok;

    if (!read_file(path, &data, &size)) {
        return false;
    }
    if (size >= 8U &&
        (memcmp(data, "!<arch>\n", 8U) == 0 ||
         memcmp(data, "!<thin>\n", 8U) == 0)) {
        archive = true;
    }
    if (!archive) {
        ok = process_elf_data(data, size, path, options, print_header);
        free(data);
        return ok;
    }
    free(data);

    {
        NmArchiveContext context;
        int status;

        context.options = options;
        context.ok = true;
        status = miniar_visit_archive(path,
                                      process_archive_member,
                                      &context,
                                      stderr);
        return status == 0 && context.ok;
    }
}

static bool parse_short_options(const char *argument, NmOptions *options) {
    size_t i;

    for (i = 1U; argument[i] != '\0'; ++i) {
        switch (argument[i]) {
        case 'n':
            options->sort_mode = NM_SORT_NUMERIC;
            break;
        case 'p':
            options->sort_mode = NM_SORT_NONE;
            break;
        case 'g':
            options->external_only = true;
            break;
        case 'u':
            options->undefined_only = true;
            break;
        case 'r':
            options->reverse = true;
            break;
        case 'A':
        case 'o':
            options->print_file_name = true;
            break;
        case 'a':
            options->debug_symbols = true;
            break;
        default:
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    NmOptions options;
    int first_file = 1;
    int i;
    int file_count;
    int failures = 0;

    memset(&options, 0, sizeof(options));
    options.sort_mode = NM_SORT_NAME;

    while (first_file < argc) {
        const char *argument = argv[first_file];

        if (strcmp(argument, "--") == 0) {
            ++first_file;
            break;
        }
        if (argument[0] != '-' || argument[1] == '\0') {
            break;
        }
        if (strcmp(argument, "--numeric-sort") == 0) {
            options.sort_mode = NM_SORT_NUMERIC;
        } else if (strcmp(argument, "--no-sort") == 0) {
            options.sort_mode = NM_SORT_NONE;
        } else if (strcmp(argument, "--extern-only") == 0) {
            options.external_only = true;
        } else if (strcmp(argument, "--undefined-only") == 0) {
            options.undefined_only = true;
        } else if (strcmp(argument, "--defined-only") == 0) {
            options.defined_only = true;
        } else if (strcmp(argument, "--reverse-sort") == 0) {
            options.reverse = true;
        } else if (strcmp(argument, "--print-file-name") == 0) {
            options.print_file_name = true;
        } else if (strcmp(argument, "--debug-syms") == 0) {
            options.debug_symbols = true;
        } else if (strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argument, "--version") == 0) {
            puts("minic-nm 0.1");
            return 0;
        } else if (argument[1] == '-') {
            fprintf(stderr, "minic-nm: unsupported option: %s\n", argument);
            return 2;
        } else if (!parse_short_options(argument, &options)) {
            fprintf(stderr, "minic-nm: unsupported option: %s\n", argument);
            return 2;
        }
        ++first_file;
    }

    if (options.undefined_only && options.defined_only) {
        fprintf(stderr,
                "minic-nm: --undefined-only and --defined-only conflict\n");
        return 2;
    }
    if (first_file >= argc) {
        usage(stderr, argv[0]);
        return 2;
    }

    file_count = argc - first_file;
    for (i = first_file; i < argc; ++i) {
        if (!process_path(argv[i], &options, file_count > 1)) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
) {
        return false;
    }
    return name[1] == 'x' || name[1] == 'd';
}

static bool should_show(const NmOptions *options,
                        const MiniElfSymbol *symbol,
                        const char *name) {
    unsigned bind = minielf_symbol_bind(symbol->info);
    unsigned type = minielf_symbol_type(symbol->info);
    bool defined = symbol->section_index != SHN_UNDEF;

    if (name[0] == '\0') {
        return false;
    }
    if (!options->debug_symbols &&
        (type == STT_SECTION || type == STT_FILE)) {
        return false;
    }
    if (options->external_only && bind == STB_LOCAL) {
        return false;
    }
    if (options->undefined_only && defined) {
        return false;
    }
    if (options->defined_only && !defined) {
        return false;
    }
    return true;
}

static int compare_name(const void *left, const void *right) {
    const NmEntry *a = left;
    const NmEntry *b = right;
    int result = strcmp(a->name, b->name);

    if (result != 0) {
        return result;
    }
    if (a->symbol.value < b->symbol.value) {
        return -1;
    }
    if (a->symbol.value > b->symbol.value) {
        return 1;
    }
    if (a->original_index < b->original_index) {
        return -1;
    }
    return a->original_index > b->original_index ? 1 : 0;
}

static int compare_numeric(const void *left, const void *right) {
    const NmEntry *a = left;
    const NmEntry *b = right;
    bool a_undefined = a->symbol.section_index == SHN_UNDEF;
    bool b_undefined = b->symbol.section_index == SHN_UNDEF;

    if (a_undefined != b_undefined) {
        return a_undefined ? -1 : 1;
    }
    if (a->symbol.value < b->symbol.value) {
        return -1;
    }
    if (a->symbol.value > b->symbol.value) {
        return 1;
    }
    return compare_name(left, right);
}

static void reverse_entries(NmEntry *entries, size_t count) {
    size_t i;

    for (i = 0U; i < count / 2U; ++i) {
        NmEntry tmp = entries[i];
        entries[i] = entries[count - 1U - i];
        entries[count - 1U - i] = tmp;
    }
}

static bool find_symbol_table(const MiniElfView *view,
                              size_t *index_out,
                              MiniElfSection *section_out) {
    size_t dynamic_index = SIZE_MAX;
    MiniElfSection dynamic_section;
    size_t i;

    for (i = 1U; i < view->section_count; ++i) {
        MiniElfSection section;

        if (!minielf_section(view, i, &section)) {
            return false;
        }
        if (section.type == SHT_SYMTAB) {
            *index_out = i;
            *section_out = section;
            return true;
        }
        if (section.type == SHT_DYNSYM && dynamic_index == SIZE_MAX) {
            dynamic_index = i;
            dynamic_section = section;
        }
    }
    if (dynamic_index != SIZE_MAX) {
        *index_out = dynamic_index;
        *section_out = dynamic_section;
        return true;
    }
    return false;
}

static void print_entry(const MiniElfView *view,
                        const NmOptions *options,
                        const char *path,
                        const NmEntry *entry) {
    int width = view->elf_class == ELFCLASS64 ? 16 : 8;
    bool undefined = entry->symbol.section_index == SHN_UNDEF;

    if (options->print_file_name) {
        printf("%s:", path);
    }
    if (undefined) {
        printf("%*s %c %s\n", width, "", entry->type, entry->name);
    } else {
        printf("%0*llx %c %s\n",
               width,
               (unsigned long long)entry->symbol.value,
               entry->type,
               entry->name);
    }
}

static bool process_elf_data(const unsigned char *data,
                             size_t size,
                             const char *display_name,
                             const NmOptions *options,
                             bool print_header) {
    MiniElfView view;
    MiniElfSection symtab;
    size_t symtab_index;
    size_t symbol_count;
    NmEntry *entries = NULL;
    size_t entry_count = 0U;
    size_t i;
    bool ok = false;

    if (!minielf_open(&view, data, size) ||
        (view.type != ET_REL && view.type != ET_EXEC && view.type != ET_DYN)) {
        fprintf(stderr,
                "minic-nm: %s: unsupported file format\n",
                display_name);
        goto done;
    }
    if (!find_symbol_table(&view, &symtab_index, &symtab)) {
        fprintf(stderr, "minic-nm: %s: no symbols\n", display_name);
        goto done;
    }
    if (symtab.link >= view.section_count) {
        fprintf(stderr,
                "minic-nm: %s: invalid symbol table\n",
                display_name);
        goto done;
    }

    symbol_count = minielf_symbol_count(&view, &symtab);
    if (symbol_count == 0U && symtab.size != 0U) {
        fprintf(stderr,
                "minic-nm: %s: invalid symbol table\n",
                display_name);
        goto done;
    }
    entries = calloc(symbol_count == 0U ? 1U : symbol_count,
                     sizeof(*entries));
    if (entries == NULL) {
        fprintf(stderr, "minic-nm: %s: out of memory\n", display_name);
        goto done;
    }

    for (i = 1U; i < symbol_count; ++i) {
        MiniElfSymbol symbol;
        const char *name;
        NmEntry *entry;

        if (!minielf_symbol(&view, symtab_index, i, &symbol) ||
            !minielf_string(&view, symtab.link, symbol.name, &name)) {
            fprintf(stderr,
                    "minic-nm: %s: invalid symbol entry\n",
                    display_name);
            goto done;
        }
        if (!should_show(options, &symbol, name)) {
            continue;
        }
        entry = &entries[entry_count++];
        entry->symbol = symbol;
        entry->name = name;
        entry->type = classify_symbol(&view, &symbol);
        entry->original_index = i;
    }

    if (options->sort_mode == NM_SORT_NAME) {
        qsort(entries, entry_count, sizeof(*entries), compare_name);
    } else if (options->sort_mode == NM_SORT_NUMERIC) {
        qsort(entries, entry_count, sizeof(*entries), compare_numeric);
    }
    if (options->reverse) {
        reverse_entries(entries, entry_count);
    }

    if (print_header && !options->print_file_name) {
        printf("\n%s:\n", display_name);
    }
    for (i = 0U; i < entry_count; ++i) {
        print_entry(&view, options, display_name, &entries[i]);
    }
    ok = true;

done:
    free(entries);
    return ok;
}

static bool process_elf_path(const char *path,
                             const char *display_name,
                             const NmOptions *options,
                             bool print_header) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool ok;

    if (!read_file(path, &data, &size)) {
        return false;
    }
    ok = process_elf_data(data,
                          size,
                          display_name,
                          options,
                          print_header);
    free(data);
    return ok;
}

typedef struct NmArchiveContext {
    const NmOptions *options;
    bool ok;
} NmArchiveContext;

static bool process_archive_member(const MiniArMemberView *member,
                                   void *context,
                                   FILE *diagnostics) {
    NmArchiveContext *archive = context;
    bool ok;

    (void)diagnostics;
    if (member->data != NULL) {
        ok = process_elf_data(member->data,
                              member->size,
                              member->name,
                              archive->options,
                              true);
    } else if (member->external_path != NULL) {
        ok = process_elf_path(member->external_path,
                              member->name,
                              archive->options,
                              true);
    } else {
        fprintf(stderr,
                "minic-nm: %s: archive member has no payload\n",
                member->name);
        ok = false;
    }
    if (!ok) {
        archive->ok = false;
    }
    return true;
}

static bool process_path(const char *path,
                         const NmOptions *options,
                         bool print_header) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool archive = false;
    bool ok;

    if (!read_file(path, &data, &size)) {
        return false;
    }
    if (size >= 8U &&
        (memcmp(data, "!<arch>\n", 8U) == 0 ||
         memcmp(data, "!<thin>\n", 8U) == 0)) {
        archive = true;
    }
    if (!archive) {
        ok = process_elf_data(data, size, path, options, print_header);
        free(data);
        return ok;
    }
    free(data);

    {
        NmArchiveContext context;
        int status;

        context.options = options;
        context.ok = true;
        status = miniar_visit_archive(path,
                                      process_archive_member,
                                      &context,
                                      stderr);
        return status == 0 && context.ok;
    }
}

static bool parse_short_options(const char *argument, NmOptions *options) {
    size_t i;

    for (i = 1U; argument[i] != '\0'; ++i) {
        switch (argument[i]) {
        case 'n':
            options->sort_mode = NM_SORT_NUMERIC;
            break;
        case 'p':
            options->sort_mode = NM_SORT_NONE;
            break;
        case 'g':
            options->external_only = true;
            break;
        case 'u':
            options->undefined_only = true;
            break;
        case 'r':
            options->reverse = true;
            break;
        case 'A':
        case 'o':
            options->print_file_name = true;
            break;
        case 'a':
            options->debug_symbols = true;
            break;
        default:
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    NmOptions options;
    int first_file = 1;
    int i;
    int file_count;
    int failures = 0;

    memset(&options, 0, sizeof(options));
    options.sort_mode = NM_SORT_NAME;

    while (first_file < argc) {
        const char *argument = argv[first_file];

        if (strcmp(argument, "--") == 0) {
            ++first_file;
            break;
        }
        if (argument[0] != '-' || argument[1] == '\0') {
            break;
        }
        if (strcmp(argument, "--numeric-sort") == 0) {
            options.sort_mode = NM_SORT_NUMERIC;
        } else if (strcmp(argument, "--no-sort") == 0) {
            options.sort_mode = NM_SORT_NONE;
        } else if (strcmp(argument, "--extern-only") == 0) {
            options.external_only = true;
        } else if (strcmp(argument, "--undefined-only") == 0) {
            options.undefined_only = true;
        } else if (strcmp(argument, "--defined-only") == 0) {
            options.defined_only = true;
        } else if (strcmp(argument, "--reverse-sort") == 0) {
            options.reverse = true;
        } else if (strcmp(argument, "--print-file-name") == 0) {
            options.print_file_name = true;
        } else if (strcmp(argument, "--debug-syms") == 0) {
            options.debug_symbols = true;
        } else if (strcmp(argument, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argument, "--version") == 0) {
            puts("minic-nm 0.1");
            return 0;
        } else if (argument[1] == '-') {
            fprintf(stderr, "minic-nm: unsupported option: %s\n", argument);
            return 2;
        } else if (!parse_short_options(argument, &options)) {
            fprintf(stderr, "minic-nm: unsupported option: %s\n", argument);
            return 2;
        }
        ++first_file;
    }

    if (options.undefined_only && options.defined_only) {
        fprintf(stderr,
                "minic-nm: --undefined-only and --defined-only conflict\n");
        return 2;
    }
    if (first_file >= argc) {
        usage(stderr, argv[0]);
        return 2;
    }

    file_count = argc - first_file;
    for (i = first_file; i < argc; ++i) {
        if (!process_path(argv[i], &options, file_count > 1)) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
