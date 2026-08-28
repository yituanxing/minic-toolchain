#include "minias_internal.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static bool grow_array(void **ptr, size_t *capacity, size_t elem_size, size_t need) {
    size_t next;
    void *p;

    if (*capacity >= need) {
        return true;
    }
    next = *capacity == 0U ? 8U : *capacity;
    while (next < need) {
        if (next > SIZE_MAX / 2U) {
            return false;
        }
        next *= 2U;
    }
    p = realloc(*ptr, next * elem_size);
    if (p == NULL) {
        return false;
    }
    *ptr = p;
    *capacity = next;
    return true;
}

char *minias_strdup(const char *text) {
    size_t n = strlen(text) + 1U;
    char *copy = malloc(n);

    if (copy != NULL) {
        memcpy(copy, text, n);
    }
    return copy;
}

char *minias_trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

void minias_set_error(MiniAs *as, const char *format, ...) {
    va_list ap;

    if (as->error[0] != '\0') {
        return;
    }
    va_start(ap, format);
    (void)vsnprintf(as->error, sizeof(as->error), format, ap);
    va_end(ap);
}

int minias_find_section(const MiniAs *as, const char *name) {
    size_t i;

    for (i = 0U; i < as->section_count; ++i) {
        if (strcmp(as->sections[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int minias_ensure_section(MiniAs *as,
                          const char *name,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t align) {
    int found = minias_find_section(as, name);
    MiniAsSection *sec;

    if (found >= 0) {
        sec = &as->sections[(size_t)found];
        if (flags != 0U) {
            sec->flags |= flags;
        }
        if (align > sec->align) {
            sec->align = align;
        }
        if (type == MINIAS_SHT_NOBITS) {
            sec->type = type;
        }
        return found;
    }
    if (!grow_array((void **)&as->sections,
                    &as->section_capacity,
                    sizeof(*as->sections),
                    as->section_count + 1U)) {
        minias_set_error(as, "out-of-memory:section");
        return -1;
    }
    sec = &as->sections[as->section_count];
    memset(sec, 0, sizeof(*sec));
    sec->name = minias_strdup(name);
    if (sec->name == NULL) {
        minias_set_error(as, "out-of-memory:section-name");
        return -1;
    }
    sec->type = type;
    sec->flags = flags;
    sec->align = align == 0U ? 1U : align;
    ++as->section_count;
    return (int)(as->section_count - 1U);
}

MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create) {
    size_t i;
    MiniAsSymbol *sym;

    for (i = 0U; i < as->symbol_count; ++i) {
        if (strcmp(as->symbols[i].name, name) == 0) {
            return &as->symbols[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (!grow_array((void **)&as->symbols,
                    &as->symbol_capacity,
                    sizeof(*as->symbols),
                    as->symbol_count + 1U)) {
        minias_set_error(as, "out-of-memory:symbol");
        return NULL;
    }
    sym = &as->symbols[as->symbol_count];
    memset(sym, 0, sizeof(*sym));
    sym->name = minias_strdup(name);
    if (sym->name == NULL) {
        minias_set_error(as, "out-of-memory:symbol-name");
        return NULL;
    }
    sym->section = MINIAS_SECTION_UNDEF;
    sym->bind = MINIAS_STB_LOCAL;
    sym->type = MINIAS_STT_NOTYPE;
    sym->visibility = MINIAS_STV_DEFAULT;
    ++as->symbol_count;
    return sym;
}

bool minias_add_relocation(MiniAs *as,
                           int section,
                           uint64_t offset,
                           uint32_t type,
                           const char *symbol_name,
                           int64_t addend) {
    MiniAsSymbol *symbol;
    MiniAsReloc *reloc;
    size_t symbol_index;

    symbol = minias_get_symbol(as, symbol_name, true);
    if (symbol == NULL) {
        return false;
    }
    symbol_index = (size_t)(symbol - as->symbols);
    if (!symbol->defined && symbol->bind == MINIAS_STB_LOCAL &&
        strncmp(symbol->name, ".L", 2U) != 0) {
        symbol->bind = MINIAS_STB_GLOBAL;
    }
    if (!grow_array((void **)&as->relocs,
                    &as->reloc_capacity,
                    sizeof(*as->relocs),
                    as->reloc_count + 1U)) {
        minias_set_error(as, "out-of-memory:relocation");
        return false;
    }
    reloc = &as->relocs[as->reloc_count++];
    reloc->section = section;
    reloc->offset = offset;
    reloc->type = type;
    reloc->symbol_index = symbol_index;
    reloc->addend = addend;
    return true;
}

bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size) {
    MiniAsSection *sec;
    size_t need;
    size_t next;
    unsigned char *p;

    if (section_index < 0 || (size_t)section_index >= as->section_count) {
        minias_set_error(as, "internal:bad-section");
        return false;
    }
    sec = &as->sections[(size_t)section_index];
    if (sec->type == MINIAS_SHT_NOBITS) {
        sec->size += size;
        return true;
    }
    if (size > SIZE_MAX - sec->size) {
        minias_set_error(as, "section-size-overflow:%s", sec->name);
        return false;
    }
    need = sec->size + size;
    if (need > sec->capacity) {
        next = sec->capacity == 0U ? 64U : sec->capacity;
        while (next < need) {
            if (next > SIZE_MAX / 2U) {
                return false;
            }
            next *= 2U;
        }
        p = realloc(sec->data, next);
        if (p == NULL) {
            minias_set_error(as, "out-of-memory:section-data");
            return false;
        }
        sec->data = p;
        sec->capacity = next;
    }
    if (size != 0U) {
        memcpy(sec->data + sec->size, data, size);
    }
    sec->size = need;
    return true;
}

bool minias_section_append_zero(MiniAs *as, int section_index, size_t size) {
    static const unsigned char zeros[64] = {0};

    while (size > 0U) {
        size_t chunk = size > sizeof(zeros) ? sizeof(zeros) : size;
        if (!minias_section_append(as, section_index, zeros, chunk)) {
            return false;
        }
        size -= chunk;
    }
    return true;
}

void minias_init(MiniAs *as) {
    memset(as, 0, sizeof(*as));
    (void)minias_ensure_section(as,
                                ".text",
                                MINIAS_SHT_PROGBITS,
                                MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR,
                                4U);
    (void)minias_ensure_section(as,
                                ".data",
                                MINIAS_SHT_PROGBITS,
                                MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                1U);
    (void)minias_ensure_section(as,
                                ".bss",
                                MINIAS_SHT_NOBITS,
                                MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                1U);
    as->current_section = minias_find_section(as, ".text");
}

void minias_destroy(MiniAs *as) {
    size_t i;

    for (i = 0U; i < as->section_count; ++i) {
        free(as->sections[i].name);
        free(as->sections[i].data);
    }
    free(as->sections);
    for (i = 0U; i < as->symbol_count; ++i) {
        free(as->symbols[i].name);
    }
    free(as->symbols);
    for (i = 0U; i < as->stmt_count; ++i) {
        free(as->stmts[i].op);
        free(as->stmts[i].args);
    }
    free(as->stmts);
    free(as->relocs);
    memset(as, 0, sizeof(*as));
}

static bool add_stmt(MiniAs *as,
                     MiniAsStmtKind kind,
                     const char *op,
                     const char *args,
                     size_t line,
                     uint32_t size,
                     uint64_t align) {
    MiniAsStmt *st;

    if (!grow_array((void **)&as->stmts,
                    &as->stmt_capacity,
                    sizeof(*as->stmts),
                    as->stmt_count + 1U)) {
        minias_set_error(as, "out-of-memory:stmt");
        return false;
    }
    st = &as->stmts[as->stmt_count];
    memset(st, 0, sizeof(*st));
    st->kind = kind;
    st->op = minias_strdup(op);
    st->args = minias_strdup(args == NULL ? "" : args);
    st->line = line;
    st->section = as->current_section;
    st->offset = (uint64_t)as->sections[(size_t)as->current_section].size;
    st->size = size;
    st->align = align;
    if (st->op == NULL || st->args == NULL) {
        minias_set_error(as, "out-of-memory:stmt-text");
        return false;
    }
    ++as->stmt_count;
    return true;
}

static void strip_comment(char *line) {
    char quote = '\0';
    bool escaped = false;
    char *p;

    for (p = line; *p != '\0'; ++p) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
                continue;
            }
            if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }
        if (*p == '#') {
            *p = '\0';
            return;
        }
    }
}

static bool parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool parse_i64_data(const char *text, int64_t *out) {
    char *end = NULL;
    long long value;

    errno = 0;
    value = strtoll(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool switch_section(MiniAs *as,
                           const char *name,
                           uint32_t type,
                           uint64_t flags,
                           uint64_t align) {
    int index = minias_ensure_section(as, name, type, flags, align);

    if (index < 0) {
        return false;
    }
    as->current_section = index;
    return true;
}

static bool parse_section_directive(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    uint64_t flags = 0U;

    if (comma != NULL) {
        *comma = '\0';
    }
    name = minias_trim(args);
    if (*name == '"') {
        size_t n = strlen(name);
        if (n >= 2U && name[n - 1U] == '"') {
            name[n - 1U] = '\0';
            ++name;
        }
    }
    if (*name == '\0') {
        minias_set_error(as, "bad-directive:.section:line=%zu", line);
        return false;
    }
    if (comma != NULL) {
        char *q = strchr(comma + 1, '"');
        if (q != NULL) {
            ++q;
            while (*q != '\0' && *q != '"') {
                if (*q == 'a') {
                    flags |= MINIAS_SHF_ALLOC;
                } else if (*q == 'w') {
                    flags |= MINIAS_SHF_WRITE;
                } else if (*q == 'x') {
                    flags |= MINIAS_SHF_EXECINSTR;
                }
                ++q;
            }
        }
    }
    if (flags == 0U) {
        if (strncmp(name, ".text", 5U) == 0) {
            flags = MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR;
        } else if (strncmp(name, ".rodata", 7U) == 0) {
            flags = MINIAS_SHF_ALLOC;
        } else if (strncmp(name, ".data", 5U) == 0 || strncmp(name, ".bss", 4U) == 0) {
            flags = MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        }
    }
    return switch_section(as,
                          name,
                          strncmp(name, ".bss", 4U) == 0 ? MINIAS_SHT_NOBITS
                                                          : MINIAS_SHT_PROGBITS,
                          flags,
                          1U);
}

static bool handle_symbol_list(MiniAs *as,
                               char *args,
                               uint8_t bind,
                               bool binding,
                               uint8_t visibility,
                               bool set_visibility) {
    char *cursor = args;

    while (cursor != NULL) {
        char *comma = strchr(cursor, ',');
        MiniAsSymbol *symbol;

        if (comma != NULL) {
            *comma = '\0';
        }
        cursor = minias_trim(cursor);
        if (*cursor != '\0') {
            symbol = minias_get_symbol(as, cursor, true);
            if (symbol == NULL) {
                return false;
            }
            if (binding) {
                symbol->bind = bind;
            }
            if (set_visibility) {
                symbol->visibility = visibility;
            }
        }
        cursor = comma == NULL ? NULL : comma + 1;
    }
    return true;
}

static bool parse_type(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    MiniAsSymbol *symbol;
    char *kind;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.type:line=%zu", line);
        return false;
    }
    *comma = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), true);
    if (symbol == NULL) {
        return false;
    }
    kind = minias_trim(comma + 1);
    if (strcmp(kind, "@function") == 0 || strcmp(kind, "%function") == 0 ||
        strcmp(kind, "STT_FUNC") == 0) {
        symbol->type = MINIAS_STT_FUNC;
    } else if (strcmp(kind, "@object") == 0 || strcmp(kind, "%object") == 0 ||
               strcmp(kind, "STT_OBJECT") == 0) {
        symbol->type = MINIAS_STT_OBJECT;
    } else {
        symbol->type = MINIAS_STT_NOTYPE;
    }
    return true;
}

static bool parse_size(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    MiniAsSymbol *symbol;
    char *expr;
    char expected[512];

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.size:line=%zu", line);
        return false;
    }
    *comma = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), false);
    if (symbol == NULL || !symbol->defined) {
        minias_set_error(as, "bad-size-symbol:%s:line=%zu", minias_trim(args), line);
        return false;
    }
    expr = minias_trim(comma + 1);
    {
        uint64_t explicit_size;
        if (parse_u64(expr, &explicit_size)) {
            symbol->size = explicit_size;
            return true;
        }
    }
    (void)snprintf(expected, sizeof(expected), ".-%s", symbol->name);
    if (strcmp(expr, expected) != 0) {
        minias_set_error(as, "unsupported-expression:.size:%s:line=%zu", expr, line);
        return false;
    }
    if (symbol->section != as->current_section) {
        minias_set_error(as, "size-section-mismatch:%s:line=%zu", symbol->name, line);
        return false;
    }
    symbol->size =
        (uint64_t)as->sections[(size_t)as->current_section].size - symbol->value;
    return true;
}

static bool handle_align(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t value;
    uint64_t pad;
    uint64_t current;
    uint64_t alignment;

    if (!parse_u64(minias_trim(args), &value)) {
        minias_set_error(as, "unsupported-expression:%s:line=%zu", op, line);
        return false;
    }
    if (strcmp(op, ".p2align") == 0 || strcmp(op, ".align") == 0) {
        if (value >= 63U) {
            minias_set_error(as, "unsupported-alignment:%s:line=%zu", args, line);
            return false;
        }
        alignment = 1ULL << value;
    } else {
        alignment = value;
    }
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U) {
        minias_set_error(as, "unsupported-alignment:%s:line=%zu", args, line);
        return false;
    }
    current = (uint64_t)as->sections[(size_t)as->current_section].size;
    pad = (alignment - (current & (alignment - 1U))) & (alignment - 1U);
    if (!add_stmt(as, MINIAS_STMT_ALIGN, op, "", line, (uint32_t)pad, alignment)) {
        return false;
    }
    as->sections[(size_t)as->current_section].size += (size_t)pad;
    if (alignment > as->sections[(size_t)as->current_section].align) {
        as->sections[(size_t)as->current_section].align = alignment;
    }
    return true;
}

static unsigned int data_width(const char *op) {
    if (strcmp(op, ".byte") == 0) {
        return 1U;
    }
    if (strcmp(op, ".half") == 0 || strcmp(op, ".short") == 0 ||
        strcmp(op, ".2byte") == 0) {
        return 2U;
    }
    if (strcmp(op, ".word") == 0 || strcmp(op, ".long") == 0 ||
        strcmp(op, ".4byte") == 0) {
        return 4U;
    }
    if (strcmp(op, ".dword") == 0 || strcmp(op, ".quad") == 0 ||
        strcmp(op, ".8byte") == 0) {
        return 8U;
    }
    return 0U;
}

static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    unsigned int width = data_width(op);
    char *copy = NULL;
    char *cursor;

    if (strcmp(op, ".zero") == 0 || strcmp(op, ".space") == 0) {
        if (!parse_u64(minias_trim(args), &count)) {
            minias_set_error(as, "unsupported-expression:%s:line=%zu", op, line);
            return false;
        }
        bytes = count;
    } else {
        copy = minias_strdup(args);
        if (copy == NULL) {
            minias_set_error(as, "out-of-memory:data-measure");
            return false;
        }
        cursor = copy;
        while (cursor != NULL) {
            char *comma = strchr(cursor, ',');
            int64_t value;

            if (comma != NULL) {
                *comma = '\0';
            }
            if (!parse_i64_data(minias_trim(cursor), &value)) {
                MiniAsSymbolExpr expr;
                if (width != 8U || !minias_parse_symbol_addend(minias_trim(cursor), &expr)) {
                    minias_set_error(as,
                                     "unsupported-expression:%s:%s:line=%zu",
                                     op,
                                     minias_trim(cursor),
                                     line);
                    free(copy);
                    return false;
                }
            }
            ++count;
            cursor = comma == NULL ? NULL : comma + 1;
        }
        free(copy);
        bytes = count * width;
    }

    if (bytes > UINT32_MAX) {
        minias_set_error(as, "data-too-large:%s:line=%zu", op, line);
        return false;
    }
    if (!add_stmt(as, MINIAS_STMT_DATA, op, args, line, (uint32_t)bytes, 1U)) {
        return false;
    }
    as->sections[(size_t)as->current_section].size += (size_t)bytes;
    return true;
}

static bool emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    unsigned int width = data_width(stmt->op);
    char *copy;
    char *cursor;

    if (strcmp(stmt->op, ".zero") == 0 || strcmp(stmt->op, ".space") == 0) {
        return minias_section_append_zero(as, stmt->section, stmt->size);
    }

    copy = minias_strdup(stmt->args);
    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:data");
        return false;
    }
    cursor = copy;
    while (cursor != NULL) {
        char *comma = strchr(cursor, ',');
        int64_t signed_value;
        uint64_t value;
        unsigned char bytes[8];
        unsigned int i;

        if (comma != NULL) {
            *comma = '\0';
        }
        if (!parse_i64_data(minias_trim(cursor), &signed_value)) {
            MiniAsSymbolExpr expr;
            uint64_t relocation_offset =
                (uint64_t)as->sections[(size_t)stmt->section].size;

            if (width != 8U || !minias_parse_symbol_addend(minias_trim(cursor), &expr)) {
                minias_set_error(as,
                                 "unsupported-expression:%s:%s:line=%zu",
                                 stmt->op,
                                 minias_trim(cursor),
                                 stmt->line);
                free(copy);
                return false;
            }
            if (!minias_section_append_zero(as, stmt->section, 8U) ||
                !minias_add_relocation(as,
                                      stmt->section,
                                      relocation_offset,
                                      MINIAS_R_RISCV_64,
                                      expr.name,
                                      expr.addend)) {
                free(copy);
                return false;
            }
        } else {
            value = (uint64_t)signed_value;
            for (i = 0U; i < width; ++i) {
                bytes[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
            }
            if (!minias_section_append(as, stmt->section, bytes, width)) {
                free(copy);
                return false;
            }
        }
        cursor = comma == NULL ? NULL : comma + 1;
    }
    free(copy);
    return true;
}

static bool process_statement(MiniAs *as, char *text, size_t line) {
    char *space;
    char *op;
    char *args;
    size_t len;
    MiniAsSymbol *symbol;
    uint32_t size;
    char reason[160];

    text = minias_trim(text);
    if (*text == '\0') {
        return true;
    }
    len = strlen(text);
    if (text[len - 1U] == ':') {
        text[len - 1U] = '\0';
        symbol = minias_get_symbol(as, minias_trim(text), true);
        if (symbol == NULL) {
            return false;
        }
        if (symbol->defined) {
            minias_set_error(as, "duplicate-symbol:%s:line=%zu", symbol->name, line);
            return false;
        }
        symbol->defined = true;
        symbol->section = as->current_section;
        symbol->value = (uint64_t)as->sections[(size_t)as->current_section].size;
        return true;
    }

    space = text;
    while (*space != '\0' && !isspace((unsigned char)*space)) {
        ++space;
    }
    if (*space != '\0') {
        *space = '\0';
        args = minias_trim(space + 1);
    } else {
        args = space;
    }
    op = text;

    if (op[0] == '.') {
        if (strcmp(op, ".text") == 0) {
            return switch_section(as,
                                  ".text",
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR,
                                  4U);
        }
        if (strcmp(op, ".data") == 0) {
            return switch_section(as,
                                  ".data",
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".bss") == 0) {
            return switch_section(as,
                                  ".bss",
                                  MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".section") == 0) {
            return parse_section_directive(as, args, line);
        }
        if (strcmp(op, ".globl") == 0 || strcmp(op, ".global") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_GLOBAL, true, 0U, false);
        }
        if (strcmp(op, ".weak") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_WEAK, true, 0U, false);
        }
        if (strcmp(op, ".hidden") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_HIDDEN, true);
        }
        if (strcmp(op, ".internal") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_INTERNAL, true);
        }
        if (strcmp(op, ".protected") == 0) {
            return handle_symbol_list(as, args, 0U, false, MINIAS_STV_PROTECTED, true);
        }
        if (strcmp(op, ".type") == 0) {
            return parse_type(as, args, line);
        }
        if (strcmp(op, ".size") == 0) {
            return parse_size(as, args, line);
        }
        if (strcmp(op, ".option") == 0 || strcmp(op, ".file") == 0 ||
            strcmp(op, ".ident") == 0) {
            return true;
        }
        if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
            strcmp(op, ".p2align") == 0) {
            return handle_align(as, op, args, line);
        }
        if (data_width(op) != 0U || strcmp(op, ".zero") == 0 ||
            strcmp(op, ".space") == 0) {
            return add_data_stmt(as, op, args, line);
        }
        minias_set_error(as, "unsupported-directive:%s:line=%zu", op, line);
        return false;
    }

    if (!minias_riscv_measure(op, args, &size, reason, sizeof(reason))) {
        minias_set_error(as, "%s:line=%zu", reason, line);
        return false;
    }
    if (!add_stmt(as, MINIAS_STMT_INSN, op, args, line, size, 1U)) {
        return false;
    }
    as->sections[(size_t)as->current_section].size += (size_t)size;
    return true;
}

bool minias_parse_file(MiniAs *as, const char *path) {
    FILE *file = fopen(path, "r");
    char linebuf[65536];
    size_t line_no = 0U;

    if (file == NULL) {
        minias_set_error(as, "input-open:%s", path);
        return false;
    }
    while (fgets(linebuf, sizeof(linebuf), file) != NULL) {
        char *cursor;
        char *semicolon;
        size_t len;

        ++line_no;
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", line_no);
            fclose(file);
            return false;
        }
        strip_comment(linebuf);
        cursor = linebuf;
        while (cursor != NULL) {
            semicolon = strchr(cursor, ';');
            if (semicolon != NULL) {
                *semicolon = '\0';
            }
            if (!process_statement(as, cursor, line_no)) {
                fclose(file);
                return false;
            }
            cursor = semicolon == NULL ? NULL : semicolon + 1;
        }
    }
    if (ferror(file)) {
        minias_set_error(as, "input-read:%s", path);
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

bool minias_emit_sections(MiniAs *as) {
    size_t i;

    for (i = 0U; i < as->section_count; ++i) {
        as->sections[i].size = 0U;
    }
    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = &as->stmts[i];
        MiniAsSection *section = &as->sections[(size_t)stmt->section];

        if (section->size != (size_t)stmt->offset) {
            minias_set_error(as, "internal-offset:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        if (stmt->kind == MINIAS_STMT_ALIGN) {
            if (!minias_section_append_zero(as, stmt->section, stmt->size)) {
                return false;
            }
        } else if (stmt->kind == MINIAS_STMT_DATA) {
            if (!emit_data_stmt(as, stmt)) {
                return false;
            }
        } else if (!minias_riscv_encode(as, stmt)) {
            return false;
        }
    }
    return true;
}

int minias_assemble_file(const char *input_path, const char *output_path, FILE *diagnostic) {
    MiniAs as;
    int rc = 1;

    minias_init(&as);
    if (as.error[0] == '\0' && minias_parse_file(&as, input_path) &&
        minias_emit_sections(&as) && minias_write_elf64(&as, output_path)) {
        rc = 0;
    }
    if (rc != 0 && diagnostic != NULL) {
        fprintf(diagnostic,
                "minic-as: %s\n",
                as.error[0] == '\0' ? "unknown-error" : as.error);
    }
    minias_destroy(&as);
    return rc;
}
