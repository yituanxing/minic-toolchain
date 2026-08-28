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
    as->previous_section = as->current_section;
    as->conditional_active = true;
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
    free(as->section_stack);
    free(as->numeric_label_counts);
    free(as->conditionals);
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
    if (index != as->current_section) {
        as->previous_section = as->current_section;
        as->current_section = index;
    }
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

static bool handle_org(MiniAs *as, const char *args, size_t line) {
    MiniAsSymbolExpr expr;
    MiniAsSymbol *target;
    uint64_t desired;
    uint64_t current;
    uint64_t pad;

    if (!minias_parse_symbol_addend(args, &expr)) {
        minias_set_error(as, "unsupported-expression:.org:%s:line=%zu", args, line);
        return false;
    }
    target = minias_get_symbol(as, expr.name, false);
    if (target == NULL || !target->defined ||
        target->section != as->current_section) {
        minias_set_error(as, "unresolved-org:%s:line=%zu", args, line);
        return false;
    }
    if (expr.addend < 0 &&
        target->value < (uint64_t)(-(expr.addend + 1)) + 1U) {
        minias_set_error(as, "org-before-zero:%s:line=%zu", args, line);
        return false;
    }
    desired = expr.addend >= 0
                  ? target->value + (uint64_t)expr.addend
                  : target->value - ((uint64_t)(-(expr.addend + 1)) + 1U);
    current = (uint64_t)as->sections[(size_t)as->current_section].size;
    if (desired < current) {
        minias_set_error(as,
                         "org-backwards:%s:current=%llu:target=%llu:line=%zu",
                         args,
                         (unsigned long long)current,
                         (unsigned long long)desired,
                         line);
        return false;
    }
    pad = desired - current;
    if (pad > UINT32_MAX) {
        minias_set_error(as, "org-gap-too-large:%s:line=%zu", args, line);
        return false;
    }
    if (!add_stmt(as,
                  MINIAS_STMT_ALIGN,
                  ".org",
                  args,
                  line,
                  (uint32_t)pad,
                  1U)) {
        return false;
    }
    as->sections[(size_t)as->current_section].size += (size_t)pad;
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

static char *strip_outer_parens(char *text) {
    text = minias_trim(text);
    for (;;) {
        size_t len = strlen(text);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || text[0] != '(' || text[len - 1U] != ')') {
            return text;
        }
        for (i = 0U; i < len; ++i) {
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                --depth;
                if (depth < 0) {
                    return text;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            return text;
        }
        text[len - 1U] = '\0';
        text = minias_trim(text + 1);
    }
}

static bool parse_if_operand(const char *text, const char **end_out, int64_t *value_out) {
    char *end = NULL;
    long long value;

    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    errno = 0;
    value = strtoll(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    *end_out = end;
    *value_out = (int64_t)value;
    return true;
}

static bool evaluate_if_expression(const char *text, bool *result) {
    const char *p;
    int64_t lhs;
    int64_t rhs;
    char op[3] = {0};
    size_t op_len = 0U;

    if (text == NULL || result == NULL ||
        !parse_if_operand(text, &p, &lhs)) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '\0') {
        *result = lhs != 0;
        return true;
    }
    if ((p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=') ||
        (p[0] == '<' && p[1] == '=') || (p[0] == '>' && p[1] == '=')) {
        op[0] = p[0];
        op[1] = p[1];
        op_len = 2U;
    } else if (*p == '<' || *p == '>') {
        op[0] = *p;
        op_len = 1U;
    } else {
        return false;
    }
    p += op_len;
    if (!parse_if_operand(p, &p, &rhs)) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '\0') {
        return false;
    }

    if (strcmp(op, "==") == 0) {
        *result = lhs == rhs;
    } else if (strcmp(op, "!=") == 0) {
        *result = lhs != rhs;
    } else if (strcmp(op, "<=") == 0) {
        *result = lhs <= rhs;
    } else if (strcmp(op, ">=") == 0) {
        *result = lhs >= rhs;
    } else if (strcmp(op, "<") == 0) {
        *result = lhs < rhs;
    } else {
        *result = lhs > rhs;
    }
    return true;
}

static bool push_conditional(MiniAs *as, const char *args, size_t line) {
    MiniAsConditional *conditional;
    bool condition_true;

    if (!evaluate_if_expression(args, &condition_true)) {
        minias_set_error(as, "unsupported-expression:.if:%s:line=%zu", args, line);
        return false;
    }
    if (!grow_array((void **)&as->conditionals,
                    &as->conditional_capacity,
                    sizeof(*as->conditionals),
                    as->conditional_count + 1U)) {
        minias_set_error(as, "out-of-memory:conditional");
        return false;
    }
    conditional = &as->conditionals[as->conditional_count++];
    conditional->parent_active = as->conditional_active;
    conditional->condition_true = condition_true;
    conditional->else_seen = false;
    as->conditional_active = conditional->parent_active && condition_true;
    return true;
}

static bool else_conditional(MiniAs *as, size_t line) {
    MiniAsConditional *conditional;

    if (as->conditional_count == 0U) {
        minias_set_error(as, "unmatched-directive:.else:line=%zu", line);
        return false;
    }
    conditional = &as->conditionals[as->conditional_count - 1U];
    if (conditional->else_seen) {
        minias_set_error(as, "duplicate-directive:.else:line=%zu", line);
        return false;
    }
    conditional->else_seen = true;
    as->conditional_active =
        conditional->parent_active && !conditional->condition_true;
    return true;
}

static bool pop_conditional(MiniAs *as, size_t line) {
    MiniAsConditional conditional;

    if (as->conditional_count == 0U) {
        minias_set_error(as, "unmatched-directive:.endif:line=%zu", line);
        return false;
    }
    conditional = as->conditionals[--as->conditional_count];
    as->conditional_active = conditional.parent_active;
    return true;
}

static bool parse_symbol_minus_dot(const char *text, MiniAsSymbolExpr *expr) {
    char *copy;
    char *normalized;
    char *minus;
    char *suffix;
    char *lhs;
    bool ok;

    if (text == NULL || expr == NULL) {
        return false;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = strip_outer_parens(copy);
    minus = strrchr(normalized, '-');
    if (minus == NULL) {
        free(copy);
        return false;
    }
    *minus = '\0';
    suffix = strip_outer_parens(minias_trim(minus + 1));
    if (strcmp(suffix, ".") != 0) {
        free(copy);
        return false;
    }
    lhs = strip_outer_parens(minias_trim(normalized));
    ok = minias_parse_symbol_addend(lhs, expr);
    free(copy);
    return ok;
}

static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    unsigned int width = data_width(op);
    char *copy = NULL;
    char *cursor;

    if (strcmp(op, ".asciz") == 0 || strcmp(op, ".string") == 0 ||
        strcmp(op, ".ascii") == 0) {
        unsigned char *decoded = NULL;
        size_t decoded_size = 0U;
        bool terminate = strcmp(op, ".ascii") != 0;

        if (!minias_decode_string_literals(args, terminate, &decoded, &decoded_size)) {
            minias_set_error(as, "unsupported-string:%s:line=%zu", op, line);
            return false;
        }
        free(decoded);
        bytes = (uint64_t)decoded_size;
    } else if (strcmp(op, ".zero") == 0 || strcmp(op, ".space") == 0) {
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
                const char *trimmed = minias_trim(cursor);
                bool supported =
                    (width == 8U && minias_parse_symbol_addend(trimmed, &expr)) ||
                    ((width == 2U || width == 4U || width == 8U) &&
                     parse_symbol_minus_dot(trimmed, &expr));

                if (!supported) {
                    minias_set_error(as,
                                     "unsupported-expression:%s:%s:line=%zu",
                                     op,
                                     trimmed,
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

static uint32_t add_relocation_type_for_width(unsigned int width) {
    return width == 2U ? MINIAS_R_RISCV_ADD16
           : width == 4U ? MINIAS_R_RISCV_ADD32
           : width == 8U ? MINIAS_R_RISCV_ADD64
                         : 0U;
}

static uint32_t sub_relocation_type_for_width(unsigned int width) {
    return width == 2U ? MINIAS_R_RISCV_SUB16
           : width == 4U ? MINIAS_R_RISCV_SUB32
           : width == 8U ? MINIAS_R_RISCV_SUB64
                         : 0U;
}

static bool emit_symbol_minus_dot(MiniAs *as,
                                  const MiniAsStmt *stmt,
                                  unsigned int width,
                                  uint64_t relocation_offset,
                                  const MiniAsSymbolExpr *expr) {
    MiniAsSymbol *target = minias_get_symbol(as, expr->name, false);
    uint32_t add_type = add_relocation_type_for_width(width);
    uint32_t sub_type = sub_relocation_type_for_width(width);

    if (target != NULL && target->defined && target->section == stmt->section) {
        int64_t difference =
            (int64_t)target->value + expr->addend - (int64_t)relocation_offset;
        uint64_t value = (uint64_t)difference;
        unsigned char bytes[8];
        unsigned int i;

        for (i = 0U; i < width; ++i) {
            bytes[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
        return minias_section_append(as, stmt->section, bytes, width);
    }

    if (add_type == 0U || sub_type == 0U) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }

    {
        char anchor_name[96];
        int written;
        MiniAsSymbol *anchor;

        written = snprintf(anchor_name,
                           sizeof(anchor_name),
                           ".Lminias_expr_%zu",
                           ++as->expr_anchor_counter);
        if (written < 0 || (size_t)written >= sizeof(anchor_name)) {
            minias_set_error(as, "expr-anchor-too-long:line=%zu", stmt->line);
            return false;
        }
        anchor = minias_get_symbol(as, anchor_name, true);
        if (anchor == NULL) {
            return false;
        }
        anchor->defined = true;
        anchor->section = stmt->section;
        anchor->value = relocation_offset;
        anchor->bind = MINIAS_STB_LOCAL;

        return minias_section_append_zero(as, stmt->section, width) &&
               minias_add_relocation(as,
                                     stmt->section,
                                     relocation_offset,
                                     add_type,
                                     expr->name,
                                     expr->addend) &&
               minias_add_relocation(as,
                                     stmt->section,
                                     relocation_offset,
                                     sub_type,
                                     anchor_name,
                                     0);
    }
}

static bool emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    unsigned int width = data_width(stmt->op);
    char *copy;
    char *cursor;

    if (strcmp(stmt->op, ".asciz") == 0 || strcmp(stmt->op, ".string") == 0 ||
        strcmp(stmt->op, ".ascii") == 0) {
        unsigned char *decoded = NULL;
        size_t decoded_size = 0U;
        bool terminate = strcmp(stmt->op, ".ascii") != 0;
        bool ok;

        if (!minias_decode_string_literals(stmt->args, terminate, &decoded, &decoded_size)) {
            minias_set_error(as, "unsupported-string:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        ok = minias_section_append(as, stmt->section, decoded, decoded_size);
        free(decoded);
        return ok;
    }
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
            const char *trimmed = minias_trim(cursor);
            uint64_t relocation_offset =
                (uint64_t)as->sections[(size_t)stmt->section].size;

            if ((width == 2U || width == 4U || width == 8U) &&
                parse_symbol_minus_dot(trimmed, &expr)) {
                if (!emit_symbol_minus_dot(as,
                                           stmt,
                                           width,
                                           relocation_offset,
                                           &expr)) {
                    free(copy);
                    return false;
                }
            } else {
                if (width != 8U ||
                    !minias_parse_symbol_addend(trimmed, &expr)) {
                    minias_set_error(as,
                                     "unsupported-expression:%s:%s:line=%zu",
                                     stmt->op,
                                     trimmed,
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

static bool parse_numeric_label_id(const char *text, size_t *id) {
    size_t value = 0U;
    const char *p = text;

    if (text == NULL || id == NULL || *text == '\0') {
        return false;
    }
    while (*p != '\0') {
        size_t digit;
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        digit = (size_t)(*p - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        ++p;
    }
    *id = value;
    return true;
}

static bool ensure_numeric_label_slot(MiniAs *as, size_t id) {
    size_t next_capacity;
    size_t *next;

    if (id < as->numeric_label_capacity) {
        return true;
    }
    next_capacity = as->numeric_label_capacity == 0U ? 16U
                                                    : as->numeric_label_capacity;
    while (next_capacity <= id) {
        if (next_capacity > SIZE_MAX / 2U) {
            minias_set_error(as, "numeric-label-too-large:%zu", id);
            return false;
        }
        next_capacity *= 2U;
    }
    next = realloc(as->numeric_label_counts,
                   next_capacity * sizeof(*as->numeric_label_counts));
    if (next == NULL) {
        minias_set_error(as, "out-of-memory:numeric-labels");
        return false;
    }
    memset(next + as->numeric_label_capacity,
           0,
           (next_capacity - as->numeric_label_capacity) * sizeof(*next));
    as->numeric_label_counts = next;
    as->numeric_label_capacity = next_capacity;
    return true;
}

static bool numeric_ref_boundary(char ch) {
    return ch == '\0' ||
           !(isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '$');
}

static bool append_rewritten_text(char **buffer,
                                  size_t *size,
                                  size_t *capacity,
                                  const char *text,
                                  size_t count) {
    size_t need = *size + count + 1U;
    char *next;
    size_t next_capacity;

    if (need > *capacity) {
        next_capacity = *capacity == 0U ? 64U : *capacity;
        while (next_capacity < need) {
            if (next_capacity > SIZE_MAX / 2U) {
                return false;
            }
            next_capacity *= 2U;
        }
        next = realloc(*buffer, next_capacity);
        if (next == NULL) {
            return false;
        }
        *buffer = next;
        *capacity = next_capacity;
    }
    memcpy(*buffer + *size, text, count);
    *size += count;
    (*buffer)[*size] = '\0';
    return true;
}

static bool rewrite_numeric_local_refs(MiniAs *as,
                                       const char *text,
                                       char **out,
                                       size_t line) {
    size_t size = 0U;
    size_t capacity = 0U;
    char *buffer = NULL;
    const char *p = text;
    char quote = '\0';
    bool escaped = false;

    while (*p != '\0') {
        if (escaped) {
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            escaped = false;
            ++p;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
            } else if (*p == quote) {
                quote = '\0';
            }
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            ++p;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
                goto oom;
            }
            ++p;
            continue;
        }
        if (isdigit((unsigned char)*p) &&
            (p == text || numeric_ref_boundary(p[-1]))) {
            const char *digits = p;
            size_t label_id = 0U;
            size_t generation;
            char replacement[96];
            int written;

            while (isdigit((unsigned char)*p)) {
                size_t digit = (size_t)(*p - '0');
                if (label_id > (SIZE_MAX - digit) / 10U) {
                    p = digits;
                    break;
                }
                label_id = label_id * 10U + digit;
                ++p;
            }
            if (p != digits && (*p == 'f' || *p == 'b') &&
                numeric_ref_boundary(p[1])) {
                if (!ensure_numeric_label_slot(as, label_id)) {
                    free(buffer);
                    return false;
                }
                if (*p == 'b') {
                    if (as->numeric_label_counts[label_id] == 0U) {
                        minias_set_error(as,
                                         "undefined-numeric-label:%zub:line=%zu",
                                         label_id,
                                         line);
                        free(buffer);
                        return false;
                    }
                    generation = as->numeric_label_counts[label_id];
                } else {
                    generation = as->numeric_label_counts[label_id] + 1U;
                }
                written = snprintf(replacement,
                                   sizeof(replacement),
                                   ".Lminias_num_%zu_%zu",
                                   label_id,
                                   generation);
                if (written < 0 || (size_t)written >= sizeof(replacement) ||
                    !append_rewritten_text(&buffer,
                                           &size,
                                           &capacity,
                                           replacement,
                                           (size_t)written)) {
                    goto oom;
                }
                ++p;
                continue;
            }
            p = digits;
        }
        if (!append_rewritten_text(&buffer, &size, &capacity, p, 1U)) {
            goto oom;
        }
        ++p;
    }

    if (buffer == NULL) {
        buffer = minias_strdup("");
        if (buffer == NULL) {
            goto oom;
        }
    }
    *out = buffer;
    return true;

oom:
    free(buffer);
    minias_set_error(as, "out-of-memory:numeric-label-rewrite");
    return false;
}

static bool push_section(MiniAs *as, char *args, size_t line) {
    if (!grow_array((void **)&as->section_stack,
                    &as->section_stack_capacity,
                    sizeof(*as->section_stack),
                    as->section_stack_count + 1U)) {
        minias_set_error(as, "out-of-memory:section-stack");
        return false;
    }
    as->section_stack[as->section_stack_count++] = as->current_section;
    if (!parse_section_directive(as, args, line)) {
        --as->section_stack_count;
        return false;
    }
    return true;
}

static bool pop_section(MiniAs *as, size_t line) {
    int old_current;

    if (as->section_stack_count == 0U) {
        minias_set_error(as, "bad-directive:.popsection:line=%zu", line);
        return false;
    }
    old_current = as->current_section;
    as->current_section = as->section_stack[--as->section_stack_count];
    as->previous_section = old_current;
    return true;
}

static bool define_label(MiniAs *as, char *label, size_t line) {
    char canonical[96];
    size_t numeric_id;
    MiniAsSymbol *symbol;

    label = minias_trim(label);
    if (*label == '\0') {
        minias_set_error(as, "empty-label:line=%zu", line);
        return false;
    }
    if (parse_numeric_label_id(label, &numeric_id)) {
        int written;
        size_t generation;

        if (!ensure_numeric_label_slot(as, numeric_id)) {
            return false;
        }
        generation = ++as->numeric_label_counts[numeric_id];
        written = snprintf(canonical,
                           sizeof(canonical),
                           ".Lminias_num_%zu_%zu",
                           numeric_id,
                           generation);
        if (written < 0 || (size_t)written >= sizeof(canonical)) {
            minias_set_error(as, "numeric-label-too-long:line=%zu", line);
            return false;
        }
        label = canonical;
    }
    symbol = minias_get_symbol(as, label, true);
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

static bool process_statement(MiniAs *as, char *text, size_t line) {
    char *space;
    char *op;
    char *args;
    uint32_t size;
    char reason[160];

    text = minias_trim(text);
    if (*text == '\0') {
        return true;
    }

    if (strncmp(text, ".if", 3U) == 0 &&
        (text[3] == '\0' || isspace((unsigned char)text[3]))) {
        return push_conditional(as, minias_trim(text + 3), line);
    }
    if (strcmp(text, ".else") == 0) {
        return else_conditional(as, line);
    }
    if (strcmp(text, ".endif") == 0) {
        return pop_conditional(as, line);
    }
    if (!as->conditional_active) {
        return true;
    }

    for (;;) {
        char *token_end = text;
        char *colon;

        while (*token_end != '\0' && *token_end != ':' &&
               !isspace((unsigned char)*token_end)) {
            ++token_end;
        }
        colon = token_end;
        while (*colon == ' ' || *colon == '\t') {
            ++colon;
        }
        if (*colon != ':') {
            break;
        }
        *token_end = '\0';
        if (!define_label(as, text, line)) {
            return false;
        }
        text = minias_trim(colon + 1);
        if (*text == '\0') {
            return true;
        }
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
        if (strcmp(op, ".extern") == 0) {
            return handle_symbol_list(as, args, MINIAS_STB_GLOBAL, true, 0U, false);
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
        if (strcmp(op, ".previous") == 0) {
            int swap = as->current_section;
            if (as->previous_section < 0 ||
                (size_t)as->previous_section >= as->section_count) {
                minias_set_error(as, "bad-directive:.previous:line=%zu", line);
                return false;
            }
            as->current_section = as->previous_section;
            as->previous_section = swap;
            return true;
        }
        if (strcmp(op, ".pushsection") == 0) {
            return push_section(as, args, line);
        }
        if (strcmp(op, ".popsection") == 0) {
            return pop_section(as, line);
        }
        if (strcmp(op, ".option") == 0 || strcmp(op, ".file") == 0 ||
            strcmp(op, ".ident") == 0) {
            return true;
        }
        if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
            strcmp(op, ".p2align") == 0) {
            return handle_align(as, op, args, line);
        }
        if (strcmp(op, ".org") == 0) {
            char *rewritten_args = NULL;
            bool ok;

            if (!rewrite_numeric_local_refs(as,
                                            args,
                                            &rewritten_args,
                                            line)) {
                return false;
            }
            ok = handle_org(as, rewritten_args, line);
            free(rewritten_args);
            return ok;
        }
        if (data_width(op) != 0U || strcmp(op, ".zero") == 0 ||
            strcmp(op, ".space") == 0 || strcmp(op, ".asciz") == 0 ||
            strcmp(op, ".string") == 0 || strcmp(op, ".ascii") == 0) {
            char *rewritten_args = NULL;
            bool ok;

            if (!rewrite_numeric_local_refs(as,
                                            args,
                                            &rewritten_args,
                                            line)) {
                return false;
            }
            ok = add_data_stmt(as, op, rewritten_args, line);
            free(rewritten_args);
            return ok;
        }
        minias_set_error(as, "unsupported-directive:%s:line=%zu", op, line);
        return false;
    }

    {
        char *rewritten_args = NULL;
        bool ok;

        if (!rewrite_numeric_local_refs(as, args, &rewritten_args, line)) {
            return false;
        }
        if (!minias_riscv_measure(op,
                                  rewritten_args,
                                  &size,
                                  reason,
                                  sizeof(reason))) {
            minias_set_error(as, "%s:line=%zu", reason, line);
            free(rewritten_args);
            return false;
        }
        ok = add_stmt(as,
                      MINIAS_STMT_INSN,
                      op,
                      rewritten_args,
                      line,
                      size,
                      1U);
        free(rewritten_args);
        if (!ok) {
            return false;
        }
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
    if (as->conditional_count != 0U) {
        minias_set_error(as,
                         "unterminated-directive:.if:depth=%zu",
                         as->conditional_count);
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
