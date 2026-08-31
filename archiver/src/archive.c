#include "miniar.h"

#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef STB_GNU_UNIQUE
#define STB_GNU_UNIQUE 10
#endif

#define MINIAR_HEADER_SIZE 60U

typedef struct MiniArMember {
    char *input_path;
    char *stored_name;
    uint64_t size;
    uint64_t mtime;
    uint64_t uid;
    uint64_t gid;
    unsigned mode;
    size_t long_name_offset;
    bool long_name;
    uint64_t header_offset;
} MiniArMember;

typedef struct MiniArSymbol {
    char *name;
    size_t member_index;
} MiniArSymbol;

typedef struct MiniArBuffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MiniArBuffer;

static char *miniar_strdup(const char *text) {
    size_t size = strlen(text) + 1U;
    char *copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static const char *skip_dot_slash(const char *path) {
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    return path;
}

static char *archive_directory(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t size;
    char *result;

    if (slash == NULL) {
        return miniar_strdup(".");
    }
    if (slash == path) {
        return miniar_strdup("/");
    }
    size = (size_t)(slash - path);
    result = malloc(size + 1U);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, path, size);
    result[size] = '\0';
    return result;
}

static char *stored_member_name(const char *archive_path,
                                const char *input_path,
                                const MiniArOptions *options) {
    const char *chosen = skip_dot_slash(input_path);
    char *directory;
    size_t directory_size;

    if (!options->thin && !options->preserve_paths) {
        return miniar_strdup(path_basename(chosen));
    }
    if (!options->thin) {
        return miniar_strdup(chosen);
    }

    directory = archive_directory(archive_path);
    if (directory == NULL) {
        return NULL;
    }
    if (strcmp(directory, ".") == 0) {
        free(directory);
        return miniar_strdup(chosen);
    }
    directory_size = strlen(directory);
    if (strncmp(chosen, directory, directory_size) == 0 &&
        chosen[directory_size] == '/') {
        chosen += directory_size + 1U;
    }
    free(directory);
    return miniar_strdup(chosen);
}

static bool range_ok(size_t offset, size_t amount, size_t total) {
    return offset <= total && amount <= total - offset;
}

static bool buffer_reserve(MiniArBuffer *buffer, size_t extra) {
    size_t required;
    size_t next;
    unsigned char *data;

    if (extra > SIZE_MAX - buffer->size) {
        return false;
    }
    required = buffer->size + extra;
    if (required <= buffer->capacity) {
        return true;
    }
    next = buffer->capacity == 0U ? 128U : buffer->capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2U) {
            next = required;
            break;
        }
        next *= 2U;
    }
    data = realloc(buffer->data, next);
    if (data == NULL) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = next;
    return true;
}

static bool buffer_append(MiniArBuffer *buffer, const void *data, size_t size) {
    if (!buffer_reserve(buffer, size)) {
        return false;
    }
    if (size != 0U) {
        memcpy(buffer->data + buffer->size, data, size);
    }
    buffer->size += size;
    return true;
}

static bool buffer_append_cstr(MiniArBuffer *buffer, const char *text) {
    return buffer_append(buffer, text, strlen(text));
}

static bool load_file(const char *path,
                      unsigned char **data_out,
                      size_t *size_out,
                      FILE *diagnostics) {
    FILE *file = fopen(path, "rb");
    long end;
    size_t size;
    unsigned char *data;

    if (file == NULL) {
        fprintf(diagnostics, "minic-ar: cannot-open:%s:%s\n", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(diagnostics, "minic-ar: cannot-size:%s\n", path);
        fclose(file);
        return false;
    }
    size = (size_t)end;
    data = malloc(size == 0U ? 1U : size);
    if (data == NULL) {
        fprintf(diagnostics, "minic-ar: out-of-memory:%s\n", path);
        fclose(file);
        return false;
    }
    if (size != 0U && fread(data, 1U, size, file) != size) {
        fprintf(diagnostics, "minic-ar: cannot-read:%s\n", path);
        free(data);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(diagnostics, "minic-ar: cannot-close:%s\n", path);
        free(data);
        return false;
    }
    *data_out = data;
    *size_out = size;
    return true;
}

static bool append_symbol(MiniArSymbol **symbols,
                          size_t *count,
                          size_t *capacity,
                          const char *name,
                          size_t member_index) {
    MiniArSymbol *next_symbols;
    char *copy;

    if (*count == *capacity) {
        size_t next = *capacity == 0U ? 32U : *capacity * 2U;
        if (next < *capacity || next > SIZE_MAX / sizeof(**symbols)) {
            return false;
        }
        next_symbols = realloc(*symbols, next * sizeof(**symbols));
        if (next_symbols == NULL) {
            return false;
        }
        *symbols = next_symbols;
        *capacity = next;
    }
    copy = miniar_strdup(name);
    if (copy == NULL) {
        return false;
    }
    (*symbols)[*count].name = copy;
    (*symbols)[*count].member_index = member_index;
    ++*count;
    return true;
}

static bool visible_bind(unsigned bind) {
    return bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE;
}

static bool collect_elf64_symbols(const unsigned char *data,
                                  size_t size,
                                  size_t member_index,
                                  MiniArSymbol **symbols,
                                  size_t *symbol_count,
                                  size_t *symbol_capacity) {
    Elf64_Ehdr ehdr;
    size_t i;

    if (!range_ok(0U, sizeof(ehdr), size)) {
        return true;
    }
    memcpy(&ehdr, data, sizeof(ehdr));
    if (ehdr.e_shentsize < sizeof(Elf64_Shdr) || ehdr.e_shnum == 0U ||
        ehdr.e_shoff > SIZE_MAX) {
        return true;
    }
    for (i = 0U; i < (size_t)ehdr.e_shnum; ++i) {
        size_t shoff = (size_t)ehdr.e_shoff + i * (size_t)ehdr.e_shentsize;
        Elf64_Shdr symtab;
        Elf64_Shdr strtab;
        size_t j;
        size_t count;

        if (!range_ok(shoff, sizeof(symtab), size)) {
            return true;
        }
        memcpy(&symtab, data + shoff, sizeof(symtab));
        if (symtab.sh_type != SHT_SYMTAB || symtab.sh_entsize < sizeof(Elf64_Sym) ||
            symtab.sh_link >= ehdr.e_shnum || symtab.sh_offset > SIZE_MAX ||
            symtab.sh_size > SIZE_MAX) {
            continue;
        }
        shoff = (size_t)ehdr.e_shoff +
                (size_t)symtab.sh_link * (size_t)ehdr.e_shentsize;
        if (!range_ok(shoff, sizeof(strtab), size)) {
            continue;
        }
        memcpy(&strtab, data + shoff, sizeof(strtab));
        if (strtab.sh_type != SHT_STRTAB || strtab.sh_offset > SIZE_MAX ||
            strtab.sh_size > SIZE_MAX ||
            !range_ok((size_t)symtab.sh_offset, (size_t)symtab.sh_size, size) ||
            !range_ok((size_t)strtab.sh_offset, (size_t)strtab.sh_size, size)) {
            continue;
        }
        count = (size_t)symtab.sh_size / (size_t)symtab.sh_entsize;
        for (j = 1U; j < count; ++j) {
            size_t off = (size_t)symtab.sh_offset + j * (size_t)symtab.sh_entsize;
            Elf64_Sym symbol;
            const char *name;
            size_t limit;

            if (!range_ok(off, sizeof(symbol), size)) {
                break;
            }
            memcpy(&symbol, data + off, sizeof(symbol));
            if (!visible_bind(ELF64_ST_BIND(symbol.st_info)) ||
                symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0U ||
                symbol.st_name >= strtab.sh_size) {
                continue;
            }
            name = (const char *)data + (size_t)strtab.sh_offset + symbol.st_name;
            limit = (size_t)strtab.sh_size - symbol.st_name;
            if (memchr(name, '\0', limit) == NULL) {
                continue;
            }
            if (!append_symbol(symbols,
                               symbol_count,
                               symbol_capacity,
                               name,
                               member_index)) {
                return false;
            }
        }
    }
    return true;
}

static bool collect_elf32_symbols(const unsigned char *data,
                                  size_t size,
                                  size_t member_index,
                                  MiniArSymbol **symbols,
                                  size_t *symbol_count,
                                  size_t *symbol_capacity) {
    Elf32_Ehdr ehdr;
    size_t i;

    if (!range_ok(0U, sizeof(ehdr), size)) {
        return true;
    }
    memcpy(&ehdr, data, sizeof(ehdr));
    if (ehdr.e_shentsize < sizeof(Elf32_Shdr) || ehdr.e_shnum == 0U) {
        return true;
    }
    for (i = 0U; i < (size_t)ehdr.e_shnum; ++i) {
        size_t shoff = (size_t)ehdr.e_shoff + i * (size_t)ehdr.e_shentsize;
        Elf32_Shdr symtab;
        Elf32_Shdr strtab;
        size_t j;
        size_t count;

        if (!range_ok(shoff, sizeof(symtab), size)) {
            return true;
        }
        memcpy(&symtab, data + shoff, sizeof(symtab));
        if (symtab.sh_type != SHT_SYMTAB || symtab.sh_entsize < sizeof(Elf32_Sym) ||
            symtab.sh_link >= ehdr.e_shnum) {
            continue;
        }
        shoff = (size_t)ehdr.e_shoff +
                (size_t)symtab.sh_link * (size_t)ehdr.e_shentsize;
        if (!range_ok(shoff, sizeof(strtab), size)) {
            continue;
        }
        memcpy(&strtab, data + shoff, sizeof(strtab));
        if (strtab.sh_type != SHT_STRTAB ||
            !range_ok((size_t)symtab.sh_offset, (size_t)symtab.sh_size, size) ||
            !range_ok((size_t)strtab.sh_offset, (size_t)strtab.sh_size, size)) {
            continue;
        }
        count = (size_t)symtab.sh_size / (size_t)symtab.sh_entsize;
        for (j = 1U; j < count; ++j) {
            size_t off = (size_t)symtab.sh_offset + j * (size_t)symtab.sh_entsize;
            Elf32_Sym symbol;
            const char *name;
            size_t limit;

            if (!range_ok(off, sizeof(symbol), size)) {
                break;
            }
            memcpy(&symbol, data + off, sizeof(symbol));
            if (!visible_bind(ELF32_ST_BIND(symbol.st_info)) ||
                symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0U ||
                symbol.st_name >= strtab.sh_size) {
                continue;
            }
            name = (const char *)data + (size_t)strtab.sh_offset + symbol.st_name;
            limit = (size_t)strtab.sh_size - symbol.st_name;
            if (memchr(name, '\0', limit) == NULL) {
                continue;
            }
            if (!append_symbol(symbols,
                               symbol_count,
                               symbol_capacity,
                               name,
                               member_index)) {
                return false;
            }
        }
    }
    return true;
}

static bool collect_member_symbols(const char *path,
                                   size_t member_index,
                                   MiniArSymbol **symbols,
                                   size_t *symbol_count,
                                   size_t *symbol_capacity,
                                   FILE *diagnostics) {
    unsigned char *data = NULL;
    size_t size = 0U;
    bool ok = true;

    if (!load_file(path, &data, &size, diagnostics)) {
        return false;
    }
    if (size >= EI_NIDENT && memcmp(data, ELFMAG, SELFMAG) == 0 &&
        data[EI_DATA] == ELFDATA2LSB) {
        if (data[EI_CLASS] == ELFCLASS64) {
            ok = collect_elf64_symbols(data,
                                       size,
                                       member_index,
                                       symbols,
                                       symbol_count,
                                       symbol_capacity);
        } else if (data[EI_CLASS] == ELFCLASS32) {
            ok = collect_elf32_symbols(data,
                                       size,
                                       member_index,
                                       symbols,
                                       symbol_count,
                                       symbol_capacity);
        }
    }
    free(data);
    if (!ok) {
        fprintf(diagnostics, "minic-ar: out-of-memory:symbol-index\n");
    }
    return ok;
}

static bool needs_long_name(const char *name) {
    return strlen(name) > 15U || strchr(name, '/') != NULL || strchr(name, ' ') != NULL;
}

static void free_members(MiniArMember *members, size_t count) {
    size_t i;
    if (members == NULL) {
        return;
    }
    for (i = 0U; i < count; ++i) {
        free(members[i].input_path);
        free(members[i].stored_name);
    }
    free(members);
}

static void free_symbols(MiniArSymbol *symbols, size_t count) {
    size_t i;
    if (symbols == NULL) {
        return;
    }
    for (i = 0U; i < count; ++i) {
        free(symbols[i].name);
    }
    free(symbols);
}

static bool prepare_members(const char *output,
                            const char *const *paths,
                            size_t count,
                            const MiniArOptions *options,
                            MiniArMember **members_out,
                            MiniArBuffer *long_names,
                            MiniArSymbol **symbols_out,
                            size_t *symbol_count_out,
                            FILE *diagnostics) {
    MiniArMember *members = calloc(count == 0U ? 1U : count, sizeof(*members));
    MiniArSymbol *symbols = NULL;
    size_t symbol_count = 0U;
    size_t symbol_capacity = 0U;
    size_t i;

    if (members == NULL) {
        fprintf(diagnostics, "minic-ar: out-of-memory:members\n");
        return false;
    }
    for (i = 0U; i < count; ++i) {
        struct stat st;
        static const char suffix[] = "/\n";

        errno = 0;
        if (stat(paths[i], &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(diagnostics,
                    "minic-ar: invalid-member:%s:%s\n",
                    paths[i],
                    errno == 0 ? "not-regular" : strerror(errno));
            goto fail;
        }
        if (st.st_size < 0) {
            fprintf(diagnostics, "minic-ar: invalid-size:%s\n", paths[i]);
            goto fail;
        }
        members[i].input_path = miniar_strdup(paths[i]);
        members[i].stored_name = stored_member_name(output, paths[i], options);
        if (members[i].input_path == NULL || members[i].stored_name == NULL) {
            fprintf(diagnostics, "minic-ar: out-of-memory:member-name\n");
            goto fail;
        }
        members[i].size = (uint64_t)st.st_size;
        members[i].mtime = options->deterministic ? 0U : (uint64_t)st.st_mtime;
        members[i].uid = options->deterministic ? 0U : (uint64_t)st.st_uid;
        members[i].gid = options->deterministic ? 0U : (uint64_t)st.st_gid;
        members[i].mode = (unsigned)st.st_mode & 07777U;
        members[i].long_name = needs_long_name(members[i].stored_name);
        if (members[i].long_name) {
            members[i].long_name_offset = long_names->size;
            if (!buffer_append_cstr(long_names, members[i].stored_name) ||
                !buffer_append(long_names, suffix, sizeof(suffix) - 1U)) {
                fprintf(diagnostics, "minic-ar: out-of-memory:long-names\n");
                goto fail;
            }
        }
        if (options->write_index &&
            !collect_member_symbols(paths[i],
                                    i,
                                    &symbols,
                                    &symbol_count,
                                    &symbol_capacity,
                                    diagnostics)) {
            goto fail;
        }
    }
    *members_out = members;
    *symbols_out = symbols;
    *symbol_count_out = symbol_count;
    return true;

fail:
    free_members(members, count);
    free_symbols(symbols, symbol_count);
    return false;
}

static uint64_t padded_size(uint64_t size) {
    return size + (size & 1U);
}

static bool compute_layout(MiniArMember *members,
                           size_t member_count,
                           const MiniArOptions *options,
                           size_t long_name_size,
                           const MiniArSymbol *symbols,
                           size_t symbol_count,
                           uint64_t *symbol_table_size_out,
                           FILE *diagnostics) {
    uint64_t cursor = 8U;
    uint64_t symbol_size = 0U;
    size_t i;

    if (options->write_index) {
        symbol_size = 4U + (uint64_t)symbol_count * 4U;
        for (i = 0U; i < symbol_count; ++i) {
            symbol_size += (uint64_t)strlen(symbols[i].name) + 1U;
        }
        symbol_size = padded_size(symbol_size);
        cursor += MINIAR_HEADER_SIZE + symbol_size;
    }
    if (long_name_size != 0U) {
        cursor += MINIAR_HEADER_SIZE + padded_size((uint64_t)long_name_size);
    }
    for (i = 0U; i < member_count; ++i) {
        members[i].header_offset = cursor;
        if (options->write_index && cursor > UINT32_MAX) {
            fprintf(diagnostics, "minic-ar: archive-index-offset-overflow\n");
            return false;
        }
        cursor += MINIAR_HEADER_SIZE;
        if (!options->thin) {
            cursor += padded_size(members[i].size);
        }
    }
    *symbol_table_size_out = symbol_size;
    return true;
}

static void write_be32(unsigned char out[4], uint32_t value) {
    out[0] = (unsigned char)(value >> 24U);
    out[1] = (unsigned char)(value >> 16U);
    out[2] = (unsigned char)(value >> 8U);
    out[3] = (unsigned char)value;
}

static bool put_field(char *header,
                      size_t offset,
                      size_t width,
                      const char *text,
                      FILE *diagnostics) {
    size_t size = strlen(text);
    if (size > width) {
        fprintf(diagnostics, "minic-ar: header-field-overflow:%s\n", text);
        return false;
    }
    memset(header + offset, ' ', width);
    memcpy(header + offset, text, size);
    return true;
}

static bool format_u64(char *buffer, size_t size, uint64_t value, unsigned base) {
    int written;
    if (base == 8U) {
        written = snprintf(buffer, size, "%" PRIo64, value);
    } else {
        written = snprintf(buffer, size, "%" PRIu64, value);
    }
    return written >= 0 && (size_t)written < size;
}

static bool write_header(FILE *file,
                         const char *name,
                         uint64_t mtime,
                         uint64_t uid,
                         uint64_t gid,
                         unsigned mode,
                         uint64_t size,
                         FILE *diagnostics) {
    char header[MINIAR_HEADER_SIZE];
    char number[64];

    memset(header, ' ', sizeof(header));
    if (!put_field(header, 0U, 16U, name, diagnostics) ||
        !format_u64(number, sizeof(number), mtime, 10U) ||
        !put_field(header, 16U, 12U, number, diagnostics) ||
        !format_u64(number, sizeof(number), uid, 10U) ||
        !put_field(header, 28U, 6U, number, diagnostics) ||
        !format_u64(number, sizeof(number), gid, 10U) ||
        !put_field(header, 34U, 6U, number, diagnostics) ||
        !format_u64(number, sizeof(number), (uint64_t)mode, 8U) ||
        !put_field(header, 40U, 8U, number, diagnostics) ||
        !format_u64(number, sizeof(number), size, 10U) ||
        !put_field(header, 48U, 10U, number, diagnostics)) {
        return false;
    }
    header[58] = '`';
    header[59] = '\n';
    if (fwrite(header, 1U, sizeof(header), file) != sizeof(header)) {
        fprintf(diagnostics, "minic-ar: write-error:header\n");
        return false;
    }
    return true;
}

static bool write_long_name_header(FILE *file,
                                   uint64_t size,
                                   FILE *diagnostics) {
    char header[MINIAR_HEADER_SIZE];
    char number[64];

    memset(header, ' ', sizeof(header));
    if (!put_field(header, 0U, 16U, "//", diagnostics) ||
        !format_u64(number, sizeof(number), size, 10U) ||
        !put_field(header, 48U, 10U, number, diagnostics)) {
        return false;
    }
    header[58] = '`';
    header[59] = '\n';
    if (fwrite(header, 1U, sizeof(header), file) != sizeof(header)) {
        fprintf(diagnostics, "minic-ar: write-error:long-name-header\n");
        return false;
    }
    return true;
}

static bool write_padding(FILE *file, uint64_t size, FILE *diagnostics) {
    if ((size & 1U) != 0U && fputc('\n', file) == EOF) {
        fprintf(diagnostics, "minic-ar: write-error:padding\n");
        return false;
    }
    return true;
}

static bool copy_file(FILE *output,
                      const char *input_path,
                      uint64_t expected_size,
                      FILE *diagnostics) {
    unsigned char buffer[65536];
    uint64_t total = 0U;
    FILE *input = fopen(input_path, "rb");

    if (input == NULL) {
        fprintf(diagnostics, "minic-ar: cannot-open:%s:%s\n", input_path, strerror(errno));
        return false;
    }
    while (!feof(input)) {
        size_t got = fread(buffer, 1U, sizeof(buffer), input);
        if (got != 0U) {
            if (fwrite(buffer, 1U, got, output) != got) {
                fprintf(diagnostics, "minic-ar: write-error:member:%s\n", input_path);
                fclose(input);
                return false;
            }
            total += (uint64_t)got;
        }
        if (ferror(input)) {
            fprintf(diagnostics, "minic-ar: read-error:%s\n", input_path);
            fclose(input);
            return false;
        }
    }
    if (fclose(input) != 0 || total != expected_size) {
        fprintf(diagnostics, "minic-ar: member-changed:%s\n", input_path);
        return false;
    }
    return true;
}

static bool write_symbol_table(FILE *file,
                               const MiniArMember *members,
                               const MiniArSymbol *symbols,
                               size_t symbol_count,
                               uint64_t symbol_table_size,
                               FILE *diagnostics) {
    unsigned char be[4];
    size_t i;

    if (!write_header(file, "/", 0U, 0U, 0U, 0U, symbol_table_size, diagnostics)) {
        return false;
    }
    if (symbol_count > UINT32_MAX) {
        fprintf(diagnostics, "minic-ar: too-many-symbols\n");
        return false;
    }
    write_be32(be, (uint32_t)symbol_count);
    if (fwrite(be, 1U, sizeof(be), file) != sizeof(be)) {
        return false;
    }
    for (i = 0U; i < symbol_count; ++i) {
        uint64_t offset = members[symbols[i].member_index].header_offset;
        if (offset > UINT32_MAX) {
            fprintf(diagnostics, "minic-ar: archive-index-offset-overflow\n");
            return false;
        }
        write_be32(be, (uint32_t)offset);
        if (fwrite(be, 1U, sizeof(be), file) != sizeof(be)) {
            return false;
        }
    }
    {
        uint64_t written_size = 4U + (uint64_t)symbol_count * 4U;
        for (i = 0U; i < symbol_count; ++i) {
            size_t length = strlen(symbols[i].name) + 1U;
            if (fwrite(symbols[i].name, 1U, length, file) != length) {
                return false;
            }
            written_size += (uint64_t)length;
        }
        while (written_size < symbol_table_size) {
            if (fputc('\0', file) == EOF) {
                fprintf(diagnostics, "minic-ar: write-error:symbol-padding\n");
                return false;
            }
            ++written_size;
        }
    }
    return true;
}

static bool write_long_name_table(FILE *file,
                                  const MiniArBuffer *long_names,
                                  FILE *diagnostics) {
    if (long_names->size == 0U) {
        return true;
    }
    if (!write_long_name_header(file,
                                (uint64_t)long_names->size,
                                diagnostics)) {
        return false;
    }
    if (fwrite(long_names->data, 1U, long_names->size, file) != long_names->size) {
        fprintf(diagnostics, "minic-ar: write-error:long-names\n");
        return false;
    }
    return write_padding(file, (uint64_t)long_names->size, diagnostics);
}

static bool write_member(FILE *file,
                         const MiniArMember *member,
                         bool thin,
                         FILE *diagnostics) {
    char name[32];
    int written;

    if (member->long_name) {
        written = snprintf(name, sizeof(name), "/%zu", member->long_name_offset);
    } else {
        written = snprintf(name, sizeof(name), "%s/", member->stored_name);
    }
    if (written < 0 || (size_t)written >= sizeof(name)) {
        fprintf(diagnostics, "minic-ar: member-name-overflow:%s\n", member->stored_name);
        return false;
    }
    if (!write_header(file,
                      name,
                      member->mtime,
                      member->uid,
                      member->gid,
                      member->mode,
                      member->size,
                      diagnostics)) {
        return false;
    }
    if (thin) {
        return true;
    }
    if (!copy_file(file, member->input_path, member->size, diagnostics)) {
        return false;
    }
    return write_padding(file, member->size, diagnostics);
}

int miniar_create_archive(const char *output_path,
                          const char *const *member_paths,
                          size_t member_count,
                          const MiniArOptions *options,
                          FILE *diagnostics) {
    MiniArMember *members = NULL;
    MiniArSymbol *symbols = NULL;
    MiniArBuffer long_names = {NULL, 0U, 0U};
    size_t symbol_count = 0U;
    uint64_t symbol_table_size = 0U;
    FILE *file = NULL;
    size_t i;
    bool ok = false;

    if (output_path == NULL || options == NULL || diagnostics == NULL) {
        return 2;
    }
    if (!prepare_members(output_path,
                         member_paths,
                         member_count,
                         options,
                         &members,
                         &long_names,
                         &symbols,
                         &symbol_count,
                         diagnostics) ||
        !compute_layout(members,
                        member_count,
                        options,
                        long_names.size,
                        symbols,
                        symbol_count,
                        &symbol_table_size,
                        diagnostics)) {
        goto done;
    }

    file = fopen(output_path, "wb");
    if (file == NULL) {
        fprintf(diagnostics, "minic-ar: cannot-create:%s:%s\n", output_path, strerror(errno));
        goto done;
    }
    {
        const char *magic = options->thin && member_count != 0U
                                ? "!<thin>\n"
                                : "!<arch>\n";
        if (fwrite(magic, 1U, 8U, file) != 8U) {
            fprintf(diagnostics, "minic-ar: write-error:magic\n");
            goto done;
        }
    }
    if (options->write_index &&
        !write_symbol_table(file,
                            members,
                            symbols,
                            symbol_count,
                            symbol_table_size,
                            diagnostics)) {
        goto done;
    }
    if (!write_long_name_table(file, &long_names, diagnostics)) {
        goto done;
    }
    for (i = 0U; i < member_count; ++i) {
        if (!write_member(file, &members[i], options->thin, diagnostics)) {
            goto done;
        }
    }
    if (fflush(file) != 0) {
        fprintf(diagnostics, "minic-ar: flush-error:%s\n", output_path);
        goto done;
    }
    ok = true;

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(output_path);
    }
    free_members(members, member_count);
    free_symbols(symbols, symbol_count);
    free(long_names.data);
    return ok ? 0 : 1;
}
