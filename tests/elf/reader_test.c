#include "minielf.h"

#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_file(const char *path, unsigned char **data_out, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(stderr, "minielf-reader-test: cannot-open:%s:%s\n",
                path,
                strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static bool symbol_name(const MiniElfView *view,
                        const MiniElfSection *symtab,
                        const MiniElfSymbol *symbol,
                        const char **name_out) {
    return minielf_string(view, symtab->link, symbol->name, name_out);
}

int main(int argc, char **argv) {
    unsigned char *data = NULL;
    size_t size = 0U;
    MiniElfView view;
    MiniElfSection symtab;
    size_t symtab_index = SIZE_MAX;
    size_t text_index = SIZE_MAX;
    size_t rela_text_index = SIZE_MAX;
    size_t symbol_count;
    size_t rela_count;
    bool have_caller = false;
    bool have_target = false;
    bool have_value = false;
    bool have_target_relocation = false;
    unsigned char expected_class;
    size_t i;
    int result = 1;

    if (argc != 3 ||
        (strcmp(argv[2], "32") != 0 && strcmp(argv[2], "64") != 0)) {
        fprintf(stderr, "usage: %s object.o 32|64\n", argv[0]);
        return 2;
    }
    expected_class = strcmp(argv[2], "64") == 0 ? ELFCLASS64 : ELFCLASS32;

    if (!read_file(argv[1], &data, &size) ||
        !minielf_open(&view, data, size) ||
        view.elf_class != expected_class ||
        view.data_encoding != ELFDATA2LSB ||
        view.type != ET_REL ||
        view.machine != EM_RISCV ||
        view.section_count == 0U) {
        fprintf(stderr, "minielf-reader-test: invalid-header:%s\n", argv[1]);
        goto done;
    }

    for (i = 1U; i < view.section_count; ++i) {
        MiniElfSection section;
        const char *name;

        if (!minielf_section(&view, i, &section) ||
            !minielf_section_name(&view, i, &name)) {
            fprintf(stderr, "minielf-reader-test: invalid-section:%zu\n", i);
            goto done;
        }
        if (strcmp(name, ".text") == 0) {
            text_index = i;
        } else if (strcmp(name, ".symtab") == 0) {
            symtab_index = i;
            symtab = section;
        } else if (strcmp(name, ".rela.text") == 0) {
            rela_text_index = i;
        }
    }
    if (text_index == SIZE_MAX ||
        symtab_index == SIZE_MAX ||
        rela_text_index == SIZE_MAX) {
        fprintf(stderr, "minielf-reader-test: missing-core-sections\n");
        goto done;
    }

    {
        const unsigned char *text;
        size_t text_size;

        if (!minielf_section_data(&view, text_index, &text, &text_size) ||
            text_size == 0U) {
            fprintf(stderr, "minielf-reader-test: invalid-text\n");
            goto done;
        }
        (void)text;
    }

    symbol_count = minielf_symbol_count(&view, &symtab);
    if (symbol_count <= 1U) {
        fprintf(stderr, "minielf-reader-test: empty-symtab\n");
        goto done;
    }
    for (i = 1U; i < symbol_count; ++i) {
        MiniElfSymbol symbol;
        const char *name;

        if (!minielf_symbol(&view, symtab_index, i, &symbol) ||
            !symbol_name(&view, &symtab, &symbol, &name)) {
            fprintf(stderr, "minielf-reader-test: invalid-symbol:%zu\n", i);
            goto done;
        }
        if (strcmp(name, "caller") == 0) {
            have_caller = symbol.section_index != SHN_UNDEF;
        } else if (strcmp(name, "target") == 0) {
            have_target = symbol.section_index != SHN_UNDEF;
        } else if (strcmp(name, "value") == 0) {
            have_value = symbol.section_index != SHN_UNDEF;
        }
    }
    if (!have_caller || !have_target || !have_value) {
        fprintf(stderr, "minielf-reader-test: missing-symbols\n");
        goto done;
    }

    {
        MiniElfSection rela_section;

        if (!minielf_section(&view, rela_text_index, &rela_section)) {
            goto done;
        }
        rela_count = minielf_rela_count(&view, &rela_section);
    }
    if (rela_count == 0U) {
        fprintf(stderr, "minielf-reader-test: missing-relocations\n");
        goto done;
    }
    for (i = 0U; i < rela_count; ++i) {
        MiniElfRela rela;
        size_t input_symbol_index;
        MiniElfSymbol symbol;
        const char *name;

        if (!minielf_rela(&view, rela_text_index, i, &rela)) {
            fprintf(stderr, "minielf-reader-test: invalid-rela:%zu\n", i);
            goto done;
        }
        input_symbol_index = minielf_rela_symbol(&view, rela.info);
        if (input_symbol_index >= symbol_count ||
            !minielf_symbol(&view,
                            symtab_index,
                            input_symbol_index,
                            &symbol) ||
            !symbol_name(&view, &symtab, &symbol, &name)) {
            fprintf(stderr, "minielf-reader-test: invalid-rela-symbol:%zu\n", i);
            goto done;
        }
        if (strcmp(name, "target") == 0 &&
            minielf_rela_type(&view, rela.info) != R_RISCV_NONE) {
            have_target_relocation = true;
        }
    }
    if (!have_target_relocation) {
        fprintf(stderr, "minielf-reader-test: target-relocation-not-found\n");
        goto done;
    }

    printf("MINIELF_READER_CASE=PASS class=%s sections=%zu symbols=%zu relas=%zu\n",
           argv[2],
           view.section_count,
           symbol_count,
           rela_count);
    result = 0;

done:
    free(data);
    return result;
}
