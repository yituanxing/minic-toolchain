#include "minias_internal.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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

static MiniAsSubsection *find_subsection(MiniAs *as, int section, uint32_t number) {
    size_t i;

    for (i = 0U; i < as->subsection_count; ++i) {
        MiniAsSubsection *sub = &as->subsections[i];
        if (sub->section == section && sub->number == number) {
            return sub;
        }
    }
    return NULL;
}

static MiniAsSubsection *ensure_subsection(MiniAs *as, int section, uint32_t number) {
    MiniAsSubsection *sub = find_subsection(as, section, number);

    if (sub != NULL) {
        return sub;
    }
    if (!grow_array((void **)&as->subsections,
                    &as->subsection_capacity,
                    sizeof(*as->subsections),
                    as->subsection_count + 1U)) {
        minias_set_error(as, "out-of-memory:subsection");
        return NULL;
    }
    sub = &as->subsections[as->subsection_count++];
    memset(sub, 0, sizeof(*sub));
    sub->section = section;
    sub->number = number;
    return sub;
}

static bool current_location(MiniAs *as, uint64_t *value) {
    MiniAsSubsection *sub;

    if (as->current_section < 0 ||
        (size_t)as->current_section >= as->section_count) {
        minias_set_error(as, "internal:bad-current-section");
        return false;
    }
    sub = ensure_subsection(as, as->current_section, as->current_subsection);
    if (sub == NULL) {
        return false;
    }
    *value = sub->size;
    return true;
}

static bool advance_current_location(MiniAs *as, uint64_t amount) {
    MiniAsSubsection *sub =
        ensure_subsection(as, as->current_section, as->current_subsection);

    if (sub == NULL) {
        return false;
    }
    if (amount > UINT64_MAX - sub->size) {
        minias_set_error(as, "subsection-size-overflow");
        return false;
    }
    sub->size += amount;
    return true;
}

static int compare_subsections(const void *lhs_ptr, const void *rhs_ptr) {
    const MiniAsSubsection *lhs = lhs_ptr;
    const MiniAsSubsection *rhs = rhs_ptr;

    if (lhs->section != rhs->section) {
        return lhs->section < rhs->section ? -1 : 1;
    }
    if (lhs->number != rhs->number) {
        return lhs->number < rhs->number ? -1 : 1;
    }
    return 0;
}

static bool finalize_subsection_layout(MiniAs *as) {
    size_t i;
    int active_section = -1;
    uint64_t cursor = 0U;

    if (as->subsection_count != 0U) {
        qsort(as->subsections,
              as->subsection_count,
              sizeof(*as->subsections),
              compare_subsections);
    }
    for (i = 0U; i < as->section_count; ++i) {
        as->sections[i].layout_size = 0U;
    }
    for (i = 0U; i < as->subsection_count; ++i) {
        MiniAsSubsection *sub = &as->subsections[i];

        if (sub->section != active_section) {
            if (active_section >= 0) {
                as->sections[(size_t)active_section].layout_size = cursor;
            }
            active_section = sub->section;
            cursor = 0U;
        }
        sub->base = cursor;
        if (sub->size > UINT64_MAX - cursor) {
            minias_set_error(as, "section-layout-overflow");
            return false;
        }
        cursor += sub->size;
    }
    if (active_section >= 0) {
        as->sections[(size_t)active_section].layout_size = cursor;
    }

    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = &as->stmts[i];
        MiniAsSubsection *sub =
            find_subsection(as, stmt->section, stmt->subsection);
        if (sub == NULL || stmt->offset > UINT64_MAX - sub->base) {
            minias_set_error(as, "internal:stmt-subsection-layout");
            return false;
        }
        stmt->offset += sub->base;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        MiniAsSymbol *symbol = &as->symbols[i];
        MiniAsSubsection *sub;

        if (!symbol->defined || symbol->section < 0) {
            continue;
        }
        sub = find_subsection(as, symbol->section, symbol->subsection);
        if (sub == NULL || symbol->value > UINT64_MAX - sub->base) {
            minias_set_error(as, "internal:symbol-subsection-layout:%s", symbol->name);
            return false;
        }
        symbol->value += sub->base;
    }
    return true;
}

static bool resolve_symbol_aliases(MiniAs *as) {
    size_t remaining = 0U;
    size_t i;

    for (i = 0U; i < as->symbol_count; ++i) {
        if (as->symbols[i].alias_pending) {
            ++remaining;
        }
    }
    while (remaining != 0U) {
        bool progress = false;

        for (i = 0U; i < as->symbol_count; ++i) {
            MiniAsSymbol *alias = &as->symbols[i];
            MiniAsSymbol *target;
            uint64_t value;

            if (!alias->alias_pending) {
                continue;
            }
            if (alias->alias_target_index >= as->symbol_count) {
                minias_set_error(as, "internal:set-target-index:%s", alias->name);
                return false;
            }
            target = &as->symbols[alias->alias_target_index];
            if (!target->defined || target->alias_pending) {
                continue;
            }
            value = target->value;
            if (alias->alias_addend >= 0) {
                uint64_t addend = (uint64_t)alias->alias_addend;
                if (addend > UINT64_MAX - value) {
                    minias_set_error(as,
                                     "set-value-overflow:%s:line=%zu",
                                     alias->name,
                                     alias->alias_line);
                    return false;
                }
                value += addend;
            } else {
                uint64_t magnitude =
                    (uint64_t)(-(alias->alias_addend + 1)) + 1U;
                if (magnitude > value) {
                    minias_set_error(as,
                                     "set-value-underflow:%s:line=%zu",
                                     alias->name,
                                     alias->alias_line);
                    return false;
                }
                value -= magnitude;
            }
            alias->defined = true;
            alias->section = target->section;
            alias->subsection = target->subsection;
            alias->value = value;
            alias->type = target->type;
            if (alias->size == 0U) {
                alias->size = target->size;
            }
            alias->alias_pending = false;
            --remaining;
            progress = true;
        }
        if (!progress) {
            for (i = 0U; i < as->symbol_count; ++i) {
                MiniAsSymbol *alias = &as->symbols[i];
                if (alias->alias_pending) {
                    MiniAsSymbol *target =
                        &as->symbols[alias->alias_target_index];
                    minias_set_error(as,
                                     "unresolved-set-target:%s:%s:line=%zu",
                                     alias->name,
                                     target->name,
                                     alias->alias_line);
                    return false;
                }
            }
        }
    }
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

static size_t symbol_name_hash(const char *name) {
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)name;

    while (*p != '\0') {
        hash ^= (uint64_t)*p++;
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool rebuild_symbol_index(MiniAs *as, size_t minimum_capacity) {
    size_t capacity = 16U;
    size_t *slots;
    size_t i;

    while (capacity < minimum_capacity) {
        if (capacity > SIZE_MAX / 2U) {
            minias_set_error(as, "symbol-index-too-large");
            return false;
        }
        capacity *= 2U;
    }
    slots = calloc(capacity, sizeof(*slots));
    if (slots == NULL) {
        minias_set_error(as, "out-of-memory:symbol-index");
        return false;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        size_t slot = symbol_name_hash(as->symbols[i].name) & (capacity - 1U);
        while (slots[slot] != 0U) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        slots[slot] = i + 1U;
    }
    free(as->symbol_slots);
    as->symbol_slots = slots;
    as->symbol_slot_capacity = capacity;
    return true;
}

static bool find_symbol_slot(const MiniAs *as,
                             const char *name,
                             size_t *slot_out,
                             size_t *index_out) {
    size_t slot;
    size_t scanned;

    if (as->symbol_slot_capacity == 0U) {
        *slot_out = 0U;
        return false;
    }
    slot = symbol_name_hash(name) & (as->symbol_slot_capacity - 1U);
    for (scanned = 0U; scanned < as->symbol_slot_capacity; ++scanned) {
        size_t encoded = as->symbol_slots[slot];
        if (encoded == 0U) {
            *slot_out = slot;
            return false;
        }
        if (strcmp(as->symbols[encoded - 1U].name, name) == 0) {
            *slot_out = slot;
            *index_out = encoded - 1U;
            return true;
        }
        slot = (slot + 1U) & (as->symbol_slot_capacity - 1U);
    }
    *slot_out = 0U;
    return false;
}

MiniAsSymbol *minias_get_symbol(MiniAs *as, const char *name, bool create) {
    size_t slot = 0U;
    size_t index = 0U;
    MiniAsSymbol *sym;

    if (find_symbol_slot(as, name, &slot, &index)) {
        return &as->symbols[index];
    }
    if (!create) {
        return NULL;
    }
    if (as->symbol_slot_capacity == 0U ||
        as->symbol_count + 1U >= as->symbol_slot_capacity / 2U) {
        size_t next_capacity =
            as->symbol_slot_capacity == 0U ? 16U
                                          : as->symbol_slot_capacity * 2U;
        if (next_capacity < as->symbol_slot_capacity ||
            !rebuild_symbol_index(as, next_capacity)) {
            return NULL;
        }
        if (find_symbol_slot(as, name, &slot, &index)) {
            return &as->symbols[index];
        }
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
    as->symbol_slots[slot] = as->symbol_count;
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
    as->current_subsection = 0U;
    as->previous_section = as->current_section;
    as->previous_subsection = 0U;
    as->conditional_active = true;
    (void)ensure_subsection(as, as->current_section, 0U);
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
    free(as->symbol_slots);
    for (i = 0U; i < as->stmt_count; ++i) {
        free(as->stmts[i].op);
        free(as->stmts[i].args);
    }
    free(as->stmts);
    free(as->relocs);
    free(as->subsections);
    free(as->section_stack);
    free(as->numeric_label_counts);
    free(as->conditionals);
    for (i = 0U; i < as->macro_count; ++i) {
        size_t j;
        free(as->macros[i].name);
        for (j = 0U; j < as->macros[i].param_count; ++j) {
            free(as->macros[i].params[j].name);
            free(as->macros[i].params[j].default_value);
        }
        free(as->macros[i].params);
        for (j = 0U; j < as->macros[i].body_count; ++j) {
            free(as->macros[i].body[j]);
        }
        free(as->macros[i].body);
        free(as->macros[i].body_lines);
    }
    free(as->macros);
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
    st->subsection = as->current_subsection;
    if (!current_location(as, &st->offset)) {
        free(st->op);
        free(st->args);
        st->op = NULL;
        st->args = NULL;
        return false;
    }
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
    char *copy = minias_strdup(text);
    char *normalized;
    char *end = NULL;
    long long value;

    if (copy == NULL) {
        return false;
    }
    normalized = minias_trim(copy);
    for (;;) {
        size_t len = strlen(normalized);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || normalized[0] != '(' ||
            normalized[len - 1U] != ')') {
            break;
        }
        for (i = 0U; i < len; ++i) {
            if (normalized[i] == '(') {
                ++depth;
            } else if (normalized[i] == ')') {
                --depth;
                if (depth < 0) {
                    encloses_all = false;
                    break;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            break;
        }
        normalized[len - 1U] = '\0';
        normalized = minias_trim(normalized + 1);
    }

    errno = 0;
    value = strtoll(normalized, &end, 0);
    if (errno != 0 || end == normalized) {
        free(copy);
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        free(copy);
        return false;
    }
    *out = (int64_t)value;
    free(copy);
    return true;
}

static bool parse_data_integer_bits(const char *text, uint64_t *out) {
    int64_t signed_value;
    char *copy;
    char *normalized;
    char *end = NULL;
    unsigned long long value;

    if (parse_i64_data(text, &signed_value)) {
        *out = (uint64_t)signed_value;
        return true;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_trim(copy);
    for (;;) {
        size_t len = strlen(normalized);
        size_t i;
        int depth = 0;
        bool encloses_all = true;

        if (len < 2U || normalized[0] != '(' ||
            normalized[len - 1U] != ')') {
            break;
        }
        for (i = 0U; i < len; ++i) {
            if (normalized[i] == '(') {
                ++depth;
            } else if (normalized[i] == ')') {
                --depth;
                if (depth < 0) {
                    encloses_all = false;
                    break;
                }
                if (depth == 0 && i + 1U != len) {
                    encloses_all = false;
                    break;
                }
            }
        }
        if (!encloses_all || depth != 0) {
            break;
        }
        normalized[len - 1U] = '\0';
        normalized = minias_trim(normalized + 1);
    }
    if (*normalized == '-') {
        free(copy);
        return false;
    }
    errno = 0;
    value = strtoull(normalized, &end, 0);
    if (errno != 0 || end == normalized) {
        free(copy);
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        free(copy);
        return false;
    }
    *out = (uint64_t)value;
    free(copy);
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
    if (index != as->current_section || as->current_subsection != 0U) {
        as->previous_section = as->current_section;
        as->previous_subsection = as->current_subsection;
        as->current_section = index;
        as->current_subsection = 0U;
    }
    return ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
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
    char *separator = strchr(args, ',');
    MiniAsSymbol *symbol;
    char *kind;

    if (separator == NULL) {
        separator = args;
        while (*separator != '\0' &&
               !isspace((unsigned char)*separator)) {
            ++separator;
        }
        if (*separator == '\0') {
            minias_set_error(as, "bad-directive:.type:line=%zu", line);
            return false;
        }
    }
    *separator = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), true);
    if (symbol == NULL) {
        return false;
    }
    kind = minias_trim(separator + 1);
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
    char compact[512];
    size_t compact_size = 0U;
    const char *p;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.size:line=%zu", line);
        return false;
    }
    *comma = '\0';
    symbol = minias_get_symbol(as, minias_trim(args), true);
    if (symbol == NULL) {
        return false;
    }
    expr = minias_trim(comma + 1);
    {
        uint64_t explicit_size;
        MiniAsSymbol *size_symbol;
        if (parse_u64(expr, &explicit_size)) {
            /*
             * GNU GCC commonly emits .size before the object's label:
             *
             *   .type object, @object
             *   .size object, 8
             * object:
             *
             * The size declaration is independent of definition order, so
             * retain it on the symbol and let the later label define value
             * and section ownership.
             */
            symbol->size = explicit_size;
            return true;
        }
        size_symbol = minias_get_symbol(as, expr, false);
        if (size_symbol != NULL && size_symbol->defined &&
            size_symbol->section == MINIAS_SECTION_ABS) {
            symbol->size = size_symbol->value;
            return true;
        }
    }
    if (!symbol->defined) {
        minias_set_error(as, "bad-size-symbol:%s:line=%zu", symbol->name, line);
        return false;
    }
    for (p = expr; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t') {
            continue;
        }
        if (compact_size + 1U >= sizeof(compact)) {
            minias_set_error(as, "unsupported-expression:.size:%s:line=%zu", expr, line);
            return false;
        }
        compact[compact_size++] = *p;
    }
    compact[compact_size] = '\0';
    (void)snprintf(expected, sizeof(expected), ".-%s", symbol->name);
    if (strcmp(compact, expected) != 0) {
        minias_set_error(as, "unsupported-expression:.size:%s:line=%zu", expr, line);
        return false;
    }
    if (symbol->section != as->current_section ||
        symbol->subsection != as->current_subsection) {
        minias_set_error(as, "size-section-mismatch:%s:line=%zu", symbol->name, line);
        return false;
    }
    {
        uint64_t here;
        if (!current_location(as, &here) || here < symbol->value) {
            minias_set_error(as, "size-location-mismatch:%s:line=%zu", symbol->name, line);
            return false;
        }
        symbol->size = here - symbol->value;
    }
    return true;
}

typedef struct MiniAsAbsoluteExpressionParser {
    MiniAs *as;
    const char *cursor;
    int64_t dot;
    bool only_absolute_symbols;
} MiniAsAbsoluteExpressionParser;

static void skip_absolute_expression_space(MiniAsAbsoluteExpressionParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') {
        ++parser->cursor;
    }
}

static bool absolute_symbol_start(char ch) {
    return ch == '.' || ch == '$' || ch == '_' || isalpha((unsigned char)ch);
}

static bool absolute_symbol_continue(char ch) {
    return absolute_symbol_start(ch) || isdigit((unsigned char)ch);
}

static bool checked_add_i64(int64_t lhs, int64_t rhs, int64_t *out) {
    if ((rhs > 0 && lhs > INT64_MAX - rhs) ||
        (rhs < 0 && lhs < INT64_MIN - rhs)) {
        return false;
    }
    *out = lhs + rhs;
    return true;
}

static bool checked_sub_i64(int64_t lhs, int64_t rhs, int64_t *out) {
    if ((rhs < 0 && lhs > INT64_MAX + rhs) ||
        (rhs > 0 && lhs < INT64_MIN + rhs)) {
        return false;
    }
    *out = lhs - rhs;
    return true;
}

static bool parse_absolute_or(MiniAsAbsoluteExpressionParser *parser, int64_t *value);

static bool parse_absolute_primary(MiniAsAbsoluteExpressionParser *parser,
                                   int64_t *value) {
    const char *start;
    char name[256];
    size_t length;
    MiniAsSymbol *symbol;
    char *end = NULL;
    long long number;

    skip_absolute_expression_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_absolute_or(parser, value)) {
            return false;
        }
        skip_absolute_expression_space(parser);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }
    if (*parser->cursor == '.' &&
        !absolute_symbol_continue(parser->cursor[1])) {
        parser->only_absolute_symbols = false;
        *value = parser->dot;
        ++parser->cursor;
        return true;
    }
    if (absolute_symbol_start(*parser->cursor)) {
        start = parser->cursor++;
        while (absolute_symbol_continue(*parser->cursor)) {
            ++parser->cursor;
        }
        length = (size_t)(parser->cursor - start);
        if (length == 0U || length >= sizeof(name)) {
            return false;
        }
        memcpy(name, start, length);
        name[length] = '\0';
        symbol = minias_get_symbol(parser->as, name, false);
        if (symbol == NULL || !symbol->defined || symbol->value > (uint64_t)INT64_MAX) {
            return false;
        }
        if (symbol->section != MINIAS_SECTION_ABS) {
            parser->only_absolute_symbols = false;
        }
        *value = (int64_t)symbol->value;
        return true;
    }

    errno = 0;
    number = strtoll(parser->cursor, &end, 0);
    if (errno != 0 || end == parser->cursor) {
        return false;
    }
    parser->cursor = end;
    *value = (int64_t)number;
    return true;
}

static bool parse_absolute_unary(MiniAsAbsoluteExpressionParser *parser,
                                 int64_t *value) {
    skip_absolute_expression_space(parser);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return parse_absolute_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        int64_t operand;
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &operand) || operand == INT64_MIN) {
            return false;
        }
        *value = -operand;
        return true;
    }
    if (*parser->cursor == '~') {
        int64_t operand;
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &operand)) {
            return false;
        }
        *value = ~operand;
        return true;
    }
    return parse_absolute_primary(parser, value);
}

static bool checked_mul_absolute_i64(int64_t lhs,
                                     int64_t rhs,
                                     int64_t *out) {
    if (lhs == 0 || rhs == 0) {
        *out = 0;
        return true;
    }
    if ((lhs == -1 && rhs == INT64_MIN) ||
        (rhs == -1 && lhs == INT64_MIN)) {
        return false;
    }
    if (lhs > 0) {
        if ((rhs > 0 && lhs > INT64_MAX / rhs) ||
            (rhs < 0 && rhs < INT64_MIN / lhs)) {
            return false;
        }
    } else {
        if ((rhs > 0 && lhs < INT64_MIN / rhs) ||
            (rhs < 0 && lhs < INT64_MAX / rhs)) {
            return false;
        }
    }
    *out = lhs * rhs;
    return true;
}

static bool parse_absolute_mul(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_unary(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        int64_t rhs;
        int64_t next;

        skip_absolute_expression_space(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &rhs)) {
            return false;
        }
        if (op == '*') {
            if (!checked_mul_absolute_i64(result, rhs, &next)) {
                return false;
            }
        } else {
            if (rhs == 0 || (result == INT64_MIN && rhs == -1)) {
                return false;
            }
            next = op == '/' ? result / rhs : result % rhs;
        }
        result = next;
    }
    *value = result;
    return true;
}

static bool parse_absolute_sum(MiniAsAbsoluteExpressionParser *parser, int64_t *value) {
    int64_t result;

    if (!parse_absolute_mul(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        int64_t rhs;
        int64_t next;

        skip_absolute_expression_space(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_mul(parser, &rhs)) {
            return false;
        }
        if (op == '+') {
            if (!checked_add_i64(result, rhs, &next)) {
                return false;
            }
        } else if (!checked_sub_i64(result, rhs, &next)) {
            return false;
        }
        result = next;
    }
    *value = result;
    return true;
}

static bool parse_absolute_shift(MiniAsAbsoluteExpressionParser *parser,
                                 int64_t *value) {
    int64_t result;

    if (!parse_absolute_sum(parser, &result)) {
        return false;
    }
    for (;;) {
        bool left;
        int64_t shift;

        skip_absolute_expression_space(parser);
        if (strncmp(parser->cursor, "<<", 2U) == 0) {
            left = true;
        } else if (strncmp(parser->cursor, ">>", 2U) == 0) {
            left = false;
        } else {
            break;
        }
        parser->cursor += 2;
        if (!parse_absolute_sum(parser, &shift) || shift < 0 || shift >= 63) {
            return false;
        }
        if (left) {
            if (result < 0 || result > (INT64_MAX >> (unsigned int)shift)) {
                return false;
            }
            result <<= (unsigned int)shift;
        } else {
            result >>= (unsigned int)shift;
        }
    }
    *value = result;
    return true;
}

static bool parse_absolute_and(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_shift(parser, &result)) {
        return false;
    }
    for (;;) {
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_shift(parser, &rhs)) {
            return false;
        }
        result &= rhs;
    }
    *value = result;
    return true;
}

static bool parse_absolute_xor(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_and(parser, &result)) {
        return false;
    }
    for (;;) {
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '^') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_and(parser, &rhs)) {
            return false;
        }
        result ^= rhs;
    }
    *value = result;
    return true;
}

static bool parse_absolute_or(MiniAsAbsoluteExpressionParser *parser,
                              int64_t *value) {
    int64_t result;

    if (!parse_absolute_xor(parser, &result)) {
        return false;
    }
    for (;;) {
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '|' || parser->cursor[1] == '|') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_xor(parser, &rhs)) {
            return false;
        }
        result |= rhs;
    }
    *value = result;
    return true;
}

static bool evaluate_absolute_expression(MiniAs *as,
                                         const char *text,
                                         int64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;

    if (!current_location(as, &dot) || dot > (uint64_t)INT64_MAX) {
        return false;
    }
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_or(&parser, value)) {
        return false;
    }
    skip_absolute_expression_space(&parser);
    return *parser.cursor == '\0';
}

static bool parse_absolute_data_bits(MiniAs *as,
                                     const char *text,
                                     uint64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;
    int64_t signed_value;

    if (parse_data_integer_bits(text, value)) {
        return true;
    }
    if (!current_location(as, &dot) || dot > (uint64_t)INT64_MAX) {
        return false;
    }
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_or(&parser, &signed_value)) {
        return false;
    }
    skip_absolute_expression_space(&parser);
    if (*parser.cursor != '\0' || !parser.only_absolute_symbols) {
        return false;
    }
    *value = (uint64_t)signed_value;
    return true;
}

static bool parse_equ(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    char *expression;
    int64_t value;
    MiniAsSymbol *symbol;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.equ:line=%zu", line);
        return false;
    }
    *comma = '\0';
    name = minias_trim(args);
    expression = minias_trim(comma + 1);
    if (*name == '\0' || *expression == '\0' ||
        !evaluate_absolute_expression(as, expression, &value) ||
        value < 0) {
        minias_set_error(as,
                         "unsupported-expression:.equ:%s:line=%zu",
                         expression,
                         line);
        return false;
    }
    symbol = minias_get_symbol(as, name, true);
    if (symbol == NULL) {
        return false;
    }
    if (symbol->defined || symbol->alias_pending) {
        if (symbol->defined && symbol->section == MINIAS_SECTION_ABS &&
            symbol->value == (uint64_t)value) {
            return true;
        }
        minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
        return false;
    }
    symbol->defined = true;
    symbol->section = MINIAS_SECTION_ABS;
    symbol->subsection = 0U;
    symbol->value = (uint64_t)value;
    return true;
}

static bool parse_set(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    char *expression;
    MiniAsSymbolExpr expr;
    MiniAsSymbol *target;
    MiniAsSymbol *alias;
    size_t target_index;

    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.set:line=%zu", line);
        return false;
    }
    *comma = '\0';
    name = minias_trim(args);
    expression = minias_trim(comma + 1);
    if (*name == '\0' || *expression == '\0') {
        minias_set_error(as,
                         "unsupported-expression:.set:%s:line=%zu",
                         expression,
                         line);
        return false;
    }

    {
        const char *tail = expression;
        bool location_constant = false;

        if (*tail == '.') {
            int64_t ignored;
            ++tail;
            while (*tail == ' ' || *tail == '\t') {
                ++tail;
            }
            if (*tail == '\0') {
                location_constant = true;
            } else if (*tail == '+' || *tail == '-') {
                ++tail;
                while (*tail == ' ' || *tail == '\t') {
                    ++tail;
                }
                location_constant = parse_i64(tail, &ignored);
            }
        }

        if (location_constant) {
            int64_t value;

            /*
             * GCC emits section anchors as:
             *
             *   .set .LANCHOR0, . + 0
             *
             * "." is the location counter, not a symbol named ".". Keep the
             * result relocatable.  Do not catch ". - symbol" here: that is an
             * absolute symbol difference and belongs to the generic evaluator.
             */
            if (!evaluate_absolute_expression(as, expression, &value) || value < 0) {
                minias_set_error(as,
                                 "unsupported-expression:.set:%s:line=%zu",
                                 expression,
                                 line);
                return false;
            }
            alias = minias_get_symbol(as, name, true);
            if (alias == NULL) {
                return false;
            }
            if (alias->defined || alias->alias_pending) {
                minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
                return false;
            }
            alias->defined = true;
            alias->section = as->current_section;
            alias->subsection = as->current_subsection;
            alias->value = (uint64_t)value;
            return true;
        }
    }

    if (minias_parse_symbol_addend(expression, &expr)) {
        target = minias_get_symbol(as, expr.name, true);
        if (target == NULL) {
            return false;
        }
        target_index = (size_t)(target - as->symbols);

        alias = minias_get_symbol(as, name, true);
        if (alias == NULL) {
            return false;
        }
        if (alias->defined || alias->alias_pending) {
            minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
            return false;
        }
        alias->alias_pending = true;
        alias->alias_target_index = target_index;
        alias->alias_addend = expr.addend;
        alias->alias_line = line;
        return true;
    }

    {
        int64_t value;
        if (!evaluate_absolute_expression(as, expression, &value) || value < 0) {
            minias_set_error(as,
                             "unsupported-expression:.set:%s:line=%zu",
                             expression,
                             line);
            return false;
        }
        alias = minias_get_symbol(as, name, true);
        if (alias == NULL) {
            return false;
        }
        if (alias->defined || alias->alias_pending) {
            minias_set_error(as, "duplicate-symbol:%s:line=%zu", name, line);
            return false;
        }
        alias->defined = true;
        alias->section = MINIAS_SECTION_ABS;
        alias->subsection = 0U;
        alias->value = (uint64_t)value;
        return true;
    }
}

static bool handle_org(MiniAs *as, const char *args, size_t line) {
    int64_t evaluated;
    uint64_t desired;
    uint64_t current;
    uint64_t pad;

    if (!evaluate_absolute_expression(as, args, &evaluated) || evaluated < 0) {
        minias_set_error(as, "unsupported-expression:.org:%s:line=%zu", args, line);
        return false;
    }
    desired = (uint64_t)evaluated;
    if (!current_location(as, &current)) {
        return false;
    }
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
    return advance_current_location(as, pad);
}

static bool handle_align(MiniAs *as, const char *op, char *args, size_t line) {
    int64_t evaluated;
    uint64_t value;
    uint64_t pad;
    uint64_t current;
    uint64_t alignment;

    if (!evaluate_absolute_expression(as, minias_trim(args), &evaluated) ||
        evaluated < 0) {
        minias_set_error(as, "unsupported-expression:%s:line=%zu", op, line);
        return false;
    }
    value = (uint64_t)evaluated;
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
    if (!current_location(as, &current)) {
        return false;
    }
    pad = (alignment - (current & (alignment - 1U))) & (alignment - 1U);
    if (!add_stmt(as, MINIAS_STMT_ALIGN, op, "", line, (uint32_t)pad, alignment)) {
        return false;
    }
    if (!advance_current_location(as, pad)) {
        return false;
    }
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

static bool parse_symbol_minus_dot_divisor(const char *text,
                                           MiniAsSymbolExpr *expr,
                                           uint64_t *divisor) {
    char *copy;
    char *normalized;
    char *slash = NULL;
    int depth = 0;
    char quote = '\0';
    char *p;
    uint64_t value;
    bool ok;

    if (text == NULL || expr == NULL || divisor == NULL) {
        return false;
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return false;
    }
    normalized = minias_trim(copy);
    for (p = normalized; *p != '\0'; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '(') {
            ++depth;
        } else if (*p == ')') {
            if (depth > 0) {
                --depth;
            }
        } else if (*p == '/' && depth == 0) {
            slash = p;
            break;
        }
    }
    if (slash == NULL) {
        free(copy);
        return false;
    }
    *slash = '\0';
    if (!parse_u64(minias_trim(slash + 1), &value) || value == 0U) {
        free(copy);
        return false;
    }
    normalized = strip_outer_parens(minias_trim(normalized));
    ok = parse_symbol_minus_dot(normalized, expr);
    if (ok) {
        *divisor = value;
    }
    free(copy);
    return ok;
}

static bool parse_symbol_difference(const char *text,
                                    MiniAsSymbolExpr *lhs_expr,
                                    MiniAsSymbolExpr *rhs_expr) {
    char *copy;
    char *normalized;
    char *minus;
    char *lhs;
    char *rhs;
    bool ok;

    if (text == NULL || lhs_expr == NULL || rhs_expr == NULL) {
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
    lhs = strip_outer_parens(minias_trim(normalized));
    rhs = strip_outer_parens(minias_trim(minus + 1));
    if (strcmp(rhs, ".") == 0) {
        free(copy);
        return false;
    }
    ok = minias_parse_symbol_addend(lhs, lhs_expr) &&
         minias_parse_symbol_addend(rhs, rhs_expr);
    free(copy);
    return ok;
}

static char *decode_incbin_path(MiniAs *as,
                                const char *args,
                                size_t line) {
    unsigned char *decoded = NULL;
    size_t decoded_size = 0U;
    char *path;

    if (!minias_decode_string_literals(args, false, &decoded, &decoded_size) ||
        decoded_size == 0U || memchr(decoded, '\0', decoded_size) != NULL) {
        free(decoded);
        minias_set_error(as, "bad-directive:.incbin:line=%zu", line);
        return NULL;
    }
    path = malloc(decoded_size + 1U);
    if (path == NULL) {
        free(decoded);
        minias_set_error(as, "out-of-memory:incbin-path");
        return NULL;
    }
    memcpy(path, decoded, decoded_size);
    path[decoded_size] = '\0';
    free(decoded);
    return path;
}

static bool measure_incbin(MiniAs *as,
                           const char *args,
                           size_t line,
                           uint64_t *bytes) {
    char *path = decode_incbin_path(as, args, line);
    FILE *file;
    long end;

    if (path == NULL) {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        minias_set_error(as, "incbin-open:%s:line=%zu", path, line);
        free(path);
        return false;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L) {
        minias_set_error(as, "incbin-size:%s:line=%zu", path, line);
        fclose(file);
        free(path);
        return false;
    }
    fclose(file);
    free(path);
    *bytes = (uint64_t)end;
    return true;
}

static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    unsigned int width = data_width(op);
    char *copy = NULL;
    char *cursor;

    if (strcmp(op, ".incbin") == 0) {
        if (!measure_incbin(as, args, line, &bytes)) {
            return false;
        }
    } else if (strcmp(op, ".asciz") == 0 || strcmp(op, ".string") == 0 ||
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
            uint64_t value;

            if (comma != NULL) {
                *comma = '\0';
            }
            if (!parse_absolute_data_bits(as, minias_trim(cursor), &value)) {
                MiniAsSymbolExpr expr;
                const char *trimmed = minias_trim(cursor);
                MiniAsSymbolExpr rhs_expr;
                uint64_t divisor;
                bool supported =
                    ((width == 2U || width == 4U || width == 8U) &&
                     minias_parse_symbol_addend(trimmed, &expr)) ||
                    ((width == 2U || width == 4U || width == 8U) &&
                     (parse_symbol_minus_dot(trimmed, &expr) ||
                      parse_symbol_minus_dot_divisor(trimmed,
                                                     &expr,
                                                     &divisor) ||
                      parse_symbol_difference(trimmed, &expr, &rhs_expr)));

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
    return advance_current_location(as, bytes);
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

static bool emit_symbol_minus_dot_divided(
    MiniAs *as,
    const MiniAsStmt *stmt,
    unsigned int width,
    uint64_t relocation_offset,
    const MiniAsSymbolExpr *expr,
    uint64_t divisor) {
    MiniAsSymbol *target = minias_get_symbol(as, expr->name, false);
    int64_t difference;
    int64_t quotient;
    uint64_t bits;
    unsigned char bytes[8];
    unsigned int i;

    if (divisor == 0U || divisor > (uint64_t)INT64_MAX ||
        target == NULL || !target->defined ||
        target->section != stmt->section ||
        target->subsection != stmt->subsection ||
        target->value > (uint64_t)INT64_MAX ||
        relocation_offset > (uint64_t)INT64_MAX) {
        minias_set_error(as,
                         "unsupported-divided-symbol-difference:%s/%llu:line=%zu",
                         expr->name,
                         (unsigned long long)divisor,
                         stmt->line);
        return false;
    }
    difference = (int64_t)target->value + expr->addend -
                 (int64_t)relocation_offset;
    quotient = difference / (int64_t)divisor;
    bits = (uint64_t)quotient;
    for (i = 0U; i < width; ++i) {
        bytes[i] = (unsigned char)((bits >> (i * 8U)) & 0xffU);
    }
    return minias_section_append(as, stmt->section, bytes, width);
}

static bool emit_symbol_difference(MiniAs *as,
                                   const MiniAsStmt *stmt,
                                   unsigned int width,
                                   uint64_t relocation_offset,
                                   const MiniAsSymbolExpr *lhs_expr,
                                   const MiniAsSymbolExpr *rhs_expr) {
    MiniAsSymbol *lhs = minias_get_symbol(as, lhs_expr->name, false);
    MiniAsSymbol *rhs = minias_get_symbol(as, rhs_expr->name, false);
    uint32_t add_type = add_relocation_type_for_width(width);
    uint32_t sub_type = sub_relocation_type_for_width(width);

    if (lhs != NULL && rhs != NULL && lhs->defined && rhs->defined &&
        lhs->section == rhs->section && lhs->section == stmt->section &&
        lhs->bind == MINIAS_STB_LOCAL && rhs->bind == MINIAS_STB_LOCAL &&
        strncmp(lhs->name, ".L", 2U) == 0 && strncmp(rhs->name, ".L", 2U) == 0) {
        int64_t difference =
            (int64_t)lhs->value + lhs_expr->addend -
            ((int64_t)rhs->value + rhs_expr->addend);
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
    return minias_section_append_zero(as, stmt->section, width) &&
           minias_add_relocation(as,
                                 stmt->section,
                                 relocation_offset,
                                 add_type,
                                 lhs_expr->name,
                                 lhs_expr->addend) &&
           minias_add_relocation(as,
                                 stmt->section,
                                 relocation_offset,
                                 sub_type,
                                 rhs_expr->name,
                                 rhs_expr->addend);
}

static bool emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    unsigned int width = data_width(stmt->op);
    char *copy;
    char *cursor;

    if (strcmp(stmt->op, ".incbin") == 0) {
        char *path = decode_incbin_path(as, stmt->args, stmt->line);
        FILE *file;
        unsigned char buffer[8192];
        uint64_t total = 0U;

        if (path == NULL) {
            return false;
        }
        file = fopen(path, "rb");
        if (file == NULL) {
            minias_set_error(as, "incbin-open:%s:line=%zu", path, stmt->line);
            free(path);
            return false;
        }
        for (;;) {
            size_t count = fread(buffer, 1U, sizeof(buffer), file);
            if (count != 0U) {
                if (total > UINT64_MAX - count ||
                    !minias_section_append(as, stmt->section, buffer, count)) {
                    fclose(file);
                    free(path);
                    return false;
                }
                total += count;
            }
            if (count < sizeof(buffer)) {
                if (ferror(file)) {
                    minias_set_error(as,
                                     "incbin-read:%s:line=%zu",
                                     path,
                                     stmt->line);
                    fclose(file);
                    free(path);
                    return false;
                }
                break;
            }
        }
        fclose(file);
        if (total != stmt->size) {
            minias_set_error(as,
                             "incbin-size-changed:%s:line=%zu",
                             path,
                             stmt->line);
            free(path);
            return false;
        }
        free(path);
        return true;
    }

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
        uint64_t value;
        unsigned char bytes[8];
        unsigned int i;

        if (comma != NULL) {
            *comma = '\0';
        }
        if (!parse_absolute_data_bits(as, minias_trim(cursor), &value)) {
            MiniAsSymbolExpr expr;
            const char *trimmed = minias_trim(cursor);
            uint64_t relocation_offset =
                (uint64_t)as->sections[(size_t)stmt->section].size;

            {
                MiniAsSymbolExpr rhs_expr;
                uint64_t divisor;
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
                } else if ((width == 2U || width == 4U || width == 8U) &&
                           parse_symbol_minus_dot_divisor(trimmed,
                                                          &expr,
                                                          &divisor)) {
                    if (!emit_symbol_minus_dot_divided(as,
                                                       stmt,
                                                       width,
                                                       relocation_offset,
                                                       &expr,
                                                       divisor)) {
                        free(copy);
                        return false;
                    }
                } else if ((width == 2U || width == 4U || width == 8U) &&
                           parse_symbol_difference(trimmed, &expr, &rhs_expr)) {
                    if (!emit_symbol_difference(as,
                                                stmt,
                                                width,
                                                relocation_offset,
                                                &expr,
                                                &rhs_expr)) {
                        free(copy);
                        return false;
                    }
                } else {
                    uint32_t relocation_type;
                    if ((width != 4U && width != 8U) ||
                        !minias_parse_symbol_addend(trimmed, &expr)) {
                        minias_set_error(as,
                                         "unsupported-expression:%s:%s:line=%zu",
                                         stmt->op,
                                         trimmed,
                                         stmt->line);
                        free(copy);
                        return false;
                    }
                    relocation_type =
                        width == 4U ? MINIAS_R_RISCV_32 : MINIAS_R_RISCV_64;
                    if (!minias_section_append_zero(as, stmt->section, width) ||
                        !minias_add_relocation(as,
                                              stmt->section,
                                              relocation_offset,
                                              relocation_type,
                                              expr.name,
                                              expr.addend)) {
                        free(copy);
                        return false;
                    }
                }
            }
        } else {
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
    as->section_stack[as->section_stack_count].section = as->current_section;
    as->section_stack[as->section_stack_count].subsection = as->current_subsection;
    ++as->section_stack_count;
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
    {
        uint32_t old_subsection = as->current_subsection;
        MiniAsSectionState restored = as->section_stack[--as->section_stack_count];

        old_current = as->current_section;
        as->current_section = restored.section;
        as->current_subsection = restored.subsection;
        as->previous_section = old_current;
        as->previous_subsection = old_subsection;
    }
    return ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
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
    if (symbol->defined || symbol->alias_pending) {
        minias_set_error(as, "duplicate-symbol:%s:line=%zu", symbol->name, line);
        return false;
    }
    symbol->defined = true;
    symbol->section = as->current_section;
    symbol->subsection = as->current_subsection;
    if (!current_location(as, &symbol->value)) {
        symbol->defined = false;
        return false;
    }
    return true;
}

static MiniAsMacro *find_macro(MiniAs *as, const char *name);
static bool expand_macro_invocation(MiniAs *as,
                                    MiniAsMacro *macro,
                                    const char *args,
                                    size_t line);

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

    {
        MiniAsMacro *macro = find_macro(as, op);
        if (macro != NULL) {
            return expand_macro_invocation(as, macro, args, line);
        }
    }

    if (op[0] == '.' && strcmp(op, ".insn") != 0) {
        /*
         * Native Linux VDSO sources use only the procedure-boundary CFI
         * surface at this stage.  Track and validate its state now; actual
         * .eh_frame byte emission is a later semantic-differential contract.
         */
        if (strcmp(op, ".cfi_startproc") == 0) {
            if (as->cfi_active ||
                (*args != '\0' && strcmp(args, "simple") != 0)) {
                minias_set_error(as, "bad-directive:.cfi_startproc:line=%zu", line);
                return false;
            }
            as->cfi_active = true;
            as->cfi_signal_frame = false;
            return true;
        }
        if (strcmp(op, ".cfi_signal_frame") == 0) {
            if (!as->cfi_active || *args != '\0') {
                minias_set_error(as, "bad-directive:.cfi_signal_frame:line=%zu", line);
                return false;
            }
            as->cfi_signal_frame = true;
            return true;
        }
        if (strcmp(op, ".cfi_endproc") == 0) {
            if (!as->cfi_active || *args != '\0') {
                minias_set_error(as, "bad-directive:.cfi_endproc:line=%zu", line);
                return false;
            }
            as->cfi_active = false;
            as->cfi_signal_frame = false;
            return true;
        }
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
        if (strcmp(op, ".equ") == 0) {
            return parse_equ(as, args, line);
        }
        if (strcmp(op, ".set") == 0) {
            return parse_set(as, args, line);
        }
        if (strcmp(op, ".previous") == 0) {
            int swap_section = as->current_section;
            uint32_t swap_subsection = as->current_subsection;
            if (as->previous_section < 0 ||
                (size_t)as->previous_section >= as->section_count) {
                minias_set_error(as, "bad-directive:.previous:line=%zu", line);
                return false;
            }
            as->current_section = as->previous_section;
            as->current_subsection = as->previous_subsection;
            as->previous_section = swap_section;
            as->previous_subsection = swap_subsection;
            return ensure_subsection(as,
                                     as->current_section,
                                     as->current_subsection) != NULL;
        }
        if (strcmp(op, ".subsection") == 0) {
            uint64_t number;
            if (!parse_u64(args, &number) || number > 8192U) {
                minias_set_error(as, "bad-directive:.subsection:%s:line=%zu", args, line);
                return false;
            }
            if ((uint32_t)number != as->current_subsection) {
                as->previous_section = as->current_section;
                as->previous_subsection = as->current_subsection;
                as->current_subsection = (uint32_t)number;
            }
            return ensure_subsection(as,
                                     as->current_section,
                                     as->current_subsection) != NULL;
        }
        if (strcmp(op, ".pushsection") == 0) {
            return push_section(as, args, line);
        }
        if (strcmp(op, ".popsection") == 0) {
            return pop_section(as, line);
        }
        if (strcmp(op, ".option") == 0 || strcmp(op, ".file") == 0 ||
            strcmp(op, ".ident") == 0 || strcmp(op, ".altmacro") == 0 ||
            strcmp(op, ".noaltmacro") == 0 || strcmp(op, ".end") == 0) {
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
            strcmp(op, ".string") == 0 || strcmp(op, ".ascii") == 0 ||
            strcmp(op, ".incbin") == 0) {
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
    return advance_current_location(as, size);
}

typedef struct MiniAsSourceLine {
    char *text;
    size_t line;
} MiniAsSourceLine;

static void destroy_source_lines(MiniAsSourceLine *lines, size_t count);
static bool valid_irp_parameter_name(const char *name);
static char *substitute_irp_parameter(MiniAs *as,
                                      const char *source,
                                      const char *name,
                                      const char *value);

enum {
    MINIAS_MACRO_NONE = 0,
    MINIAS_MACRO_BEGIN = 1,
    MINIAS_MACRO_END = 2
};

static int classify_macro_line(const char *text,
                               char *argument,
                               size_t argument_size) {
    char *copy = minias_strdup(text);
    char *trimmed;
    const char *rest = NULL;
    int kind = MINIAS_MACRO_NONE;

    if (argument_size != 0U) {
        argument[0] = '\0';
    }
    if (copy == NULL) {
        return -1;
    }
    strip_comment(copy);
    trimmed = minias_trim(copy);
    if (strncmp(trimmed, ".macro", 6U) == 0 &&
        (trimmed[6] == '\0' || isspace((unsigned char)trimmed[6]))) {
        rest = trimmed + 6;
        kind = MINIAS_MACRO_BEGIN;
    } else if (strcmp(trimmed, ".endm") == 0 ||
               strcmp(trimmed, ".endmacro") == 0) {
        kind = MINIAS_MACRO_END;
    }
    if (rest != NULL) {
        while (*rest == ' ' || *rest == '\t') {
            ++rest;
        }
        if (argument_size != 0U) {
            int written = snprintf(argument, argument_size, "%s", rest);
            if (written < 0 || (size_t)written >= argument_size) {
                free(copy);
                return -1;
            }
        }
    }
    free(copy);
    return kind;
}

static size_t split_macro_tokens(char *text, char **tokens, size_t capacity) {
    size_t count = 0U;
    char *p = text;

    while (*p != '\0') {
        char *start;
        int depth = 0;
        char quote = '\0';

        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (count == capacity) {
            return SIZE_MAX;
        }
        start = p;
        while (*p != '\0') {
            if (quote != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == quote) {
                    quote = '\0';
                }
                ++p;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                quote = *p++;
                continue;
            }
            if (*p == '(' || *p == '[' || *p == '{') {
                ++depth;
                ++p;
                continue;
            }
            if (*p == ')' || *p == ']' || *p == '}') {
                if (depth > 0) {
                    --depth;
                }
                ++p;
                continue;
            }
            if (depth == 0 &&
                (*p == ',' || *p == ' ' || *p == '\t')) {
                break;
            }
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
        tokens[count++] = start;
    }
    return count;
}

static bool macro_has_top_level_comma(const char *text) {
    int depth = 0;
    char quote = '\0';

    for (; *text != '\0'; ++text) {
        if (quote != '\0') {
            if (*text == '\\' && text[1] != '\0') {
                ++text;
                continue;
            }
            if (*text == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*text == '\'' || *text == '"') {
            quote = *text;
        } else if (*text == '(' || *text == '[' || *text == '{') {
            ++depth;
        } else if (*text == ')' || *text == ']' || *text == '}') {
            if (depth > 0) {
                --depth;
            }
        } else if (*text == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}

static char *find_macro_argument_space(char *text) {
    int depth = 0;
    char quote = '\0';
    char *p;

    for (p = text; *p != '\0'; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
                continue;
            }
            if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '(' || *p == '[' || *p == '{') {
            ++depth;
        } else if (*p == ')' || *p == ']' || *p == '}') {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0 && (*p == ' ' || *p == '\t')) {
            return p;
        }
    }
    return NULL;
}

static size_t split_macro_arguments(char *text,
                                    char **tokens,
                                    size_t capacity,
                                    size_t expected_count) {
    char *segments[64];
    size_t segment_count = 0U;
    size_t count = 0U;
    char *cursor = text;
    size_t needed;

    if (!macro_has_top_level_comma(text)) {
        return split_macro_tokens(text, tokens, capacity);
    }

    while (*cursor != '\0') {
        char *start;
        char *p;
        int depth = 0;
        char quote = '\0';

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (segment_count == sizeof(segments) / sizeof(segments[0])) {
            return SIZE_MAX;
        }
        start = cursor;
        p = cursor;
        while (*p != '\0') {
            if (quote != '\0') {
                if (*p == '\\' && p[1] != '\0') {
                    p += 2;
                    continue;
                }
                if (*p == quote) {
                    quote = '\0';
                }
                ++p;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                quote = *p++;
                continue;
            }
            if (*p == '(' || *p == '[' || *p == '{') {
                ++depth;
                ++p;
                continue;
            }
            if (*p == ')' || *p == ']' || *p == '}') {
                if (depth > 0) {
                    --depth;
                }
                ++p;
                continue;
            }
            if (*p == ',' && depth == 0) {
                break;
            }
            ++p;
        }
        if (*p == ',') {
            *p++ = '\0';
        }
        segments[segment_count++] = minias_trim(start);
        cursor = p;
    }

    needed = expected_count > segment_count
                 ? expected_count - segment_count
                 : 0U;
    for (size_t i = 0U; i < segment_count; ++i) {
        char *segment = segments[i];

        while (needed != 0U) {
            char *space = find_macro_argument_space(segment);
            char *rest;

            if (space == NULL) {
                break;
            }
            *space = '\0';
            rest = space + 1U;
            while (*rest == ' ' || *rest == '\t') {
                ++rest;
            }
            if (*segment == '\0' || *rest == '\0') {
                segment = rest;
                continue;
            }
            if (count == capacity) {
                return SIZE_MAX;
            }
            tokens[count++] = minias_trim(segment);
            segment = rest;
            --needed;
        }
        if (*segment != '\0') {
            if (count == capacity) {
                return SIZE_MAX;
            }
            tokens[count++] = minias_trim(segment);
        }
    }
    return count;
}

static MiniAsMacro *find_macro(MiniAs *as, const char *name) {
    size_t i;

    for (i = 0U; i < as->macro_count; ++i) {
        if (strcmp(as->macros[i].name, name) == 0) {
            return &as->macros[i];
        }
    }
    return NULL;
}

static void compact_macro_parameter_spacing(char *text) {
    char *read = text;
    char *write = text;

    while (*read != '\0') {
        if (*read == '=' || *read == ':') {
            while (write > text && (write[-1] == ' ' || write[-1] == '\t')) {
                --write;
            }
            *write++ = *read++;
            while (*read == ' ' || *read == '\t') {
                ++read;
            }
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

static bool define_macro(MiniAs *as,
                         const char *argument,
                         MiniAsSourceLine *lines,
                         size_t count,
                         size_t line) {
    char *copy = minias_strdup(argument);
    char *tokens[64];
    size_t token_count;
    MiniAsMacro *macro;
    size_t i;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:macro-definition");
        return false;
    }
    compact_macro_parameter_spacing(copy);
    token_count = split_macro_tokens(copy, tokens, 64U);
    if (token_count == SIZE_MAX || token_count == 0U ||
        !valid_irp_parameter_name(tokens[0])) {
        minias_set_error(as, "bad-directive:.macro:line=%zu", line);
        free(copy);
        return false;
    }
    if (find_macro(as, tokens[0]) != NULL) {
        minias_set_error(as, "duplicate-macro:%s:line=%zu", tokens[0], line);
        free(copy);
        return false;
    }
    if (!grow_array((void **)&as->macros,
                    &as->macro_capacity,
                    sizeof(*as->macros),
                    as->macro_count + 1U)) {
        minias_set_error(as, "out-of-memory:macro");
        free(copy);
        return false;
    }
    macro = &as->macros[as->macro_count];
    memset(macro, 0, sizeof(*macro));
    macro->name = minias_strdup(tokens[0]);
    if (macro->name == NULL) {
        minias_set_error(as, "out-of-memory:macro-name");
        free(copy);
        return false;
    }
    if (token_count > 1U) {
        macro->params = calloc(token_count - 1U, sizeof(*macro->params));
        if (macro->params == NULL) {
            minias_set_error(as, "out-of-memory:macro-params");
            free(copy);
            return false;
        }
    }
    macro->param_count = token_count - 1U;
    for (i = 1U; i < token_count; ++i) {
        char *spec = tokens[i];
        char *qualifier = strchr(spec, ':');
        char *equal = strchr(spec, '=');
        char *end = NULL;

        if (qualifier != NULL && (equal == NULL || qualifier < equal)) {
            *qualifier++ = '\0';
            end = qualifier - 1;
        } else if (equal != NULL) {
            *equal++ = '\0';
            end = equal - 1;
        }
        if (!valid_irp_parameter_name(spec)) {
            minias_set_error(as, "bad-macro-parameter:%s:line=%zu",
                             spec, line);
            free(copy);
            return false;
        }
        macro->params[i - 1U].name = minias_strdup(spec);
        if (macro->params[i - 1U].name == NULL) {
            minias_set_error(as, "out-of-memory:macro-param-name");
            free(copy);
            return false;
        }
        if (qualifier != NULL) {
            if (strcmp(qualifier, "req") != 0) {
                minias_set_error(as, "unsupported-macro-qualifier:%s:line=%zu",
                                 qualifier, line);
                free(copy);
                return false;
            }
            macro->params[i - 1U].required = true;
        }
        if (equal != NULL) {
            macro->params[i - 1U].default_value = minias_strdup(equal);
            if (macro->params[i - 1U].default_value == NULL) {
                minias_set_error(as, "out-of-memory:macro-param-default");
                free(copy);
                return false;
            }
        }
        (void)end;
    }
    if (count != 0U) {
        macro->body = calloc(count, sizeof(*macro->body));
        macro->body_lines = calloc(count, sizeof(*macro->body_lines));
        if (macro->body == NULL || macro->body_lines == NULL) {
            minias_set_error(as, "out-of-memory:macro-body");
            free(copy);
            return false;
        }
    }
    macro->body_count = count;
    for (i = 0U; i < count; ++i) {
        macro->body[i] = minias_strdup(lines[i].text);
        macro->body_lines[i] = lines[i].line;
        if (macro->body[i] == NULL) {
            minias_set_error(as, "out-of-memory:macro-body-line");
            free(copy);
            return false;
        }
    }
    ++as->macro_count;
    free(copy);
    return true;
}

static char *substitute_macro_counter(MiniAs *as,
                                      const char *source,
                                      size_t counter) {
    char counter_text[32];
    size_t counter_len;
    size_t occurrences = 0U;
    size_t i;
    size_t source_len = strlen(source);
    char *output;
    size_t out = 0U;

    (void)snprintf(counter_text, sizeof(counter_text), "%zu", counter);
    counter_len = strlen(counter_text);
    for (i = 0U; i + 1U < source_len; ++i) {
        if (source[i] == '\\' && source[i + 1U] == '@') {
            ++occurrences;
            ++i;
        }
    }
    if (occurrences == 0U) {
        return minias_strdup(source);
    }
    if (counter_len > 2U &&
        occurrences > (SIZE_MAX - source_len) / (counter_len - 2U)) {
        minias_set_error(as, "source-size-overflow:macro-counter");
        return NULL;
    }
    output = malloc(source_len + occurrences * (counter_len - 2U) + 1U);
    if (output == NULL) {
        minias_set_error(as, "out-of-memory:macro-counter");
        return NULL;
    }
    for (i = 0U; i < source_len; ++i) {
        if (source[i] == '\\' && i + 1U < source_len &&
            source[i + 1U] == '@') {
            memcpy(output + out, counter_text, counter_len);
            out += counter_len;
            ++i;
        } else {
            output[out++] = source[i];
        }
    }
    output[out] = '\0';
    return output;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end);

static char *strip_macro_argument_quotes(char *text) {
    size_t len;

    if (text == NULL) {
        return text;
    }
    len = strlen(text);
    if (len >= 2U &&
        ((text[0] == '"' && text[len - 1U] == '"') ||
         (text[0] == '\'' && text[len - 1U] == '\''))) {
        memmove(text, text + 1U, len - 2U);
        text[len - 2U] = '\0';
    }
    return text;
}

static bool expand_macro_invocation(MiniAs *as,
                                    MiniAsMacro *macro,
                                    const char *args,
                                    size_t line) {
    char *copy = minias_strdup(args);
    char *tokens[64];
    const char *values[64];
    size_t supplied;
    MiniAsSourceLine *expanded = NULL;
    size_t i;
    size_t counter;
    bool ok = false;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:macro-arguments");
        return false;
    }
    supplied = split_macro_arguments(copy,
                                     tokens,
                                     64U,
                                     macro->param_count);
    if (supplied == SIZE_MAX || supplied > macro->param_count) {
        minias_set_error(as, "bad-macro-arguments:%s:line=%zu",
                         macro->name, line);
        free(copy);
        return false;
    }
    for (i = 0U; i < macro->param_count; ++i) {
        if (i < supplied) {
            values[i] = strip_macro_argument_quotes(tokens[i]);
        } else if (macro->params[i].default_value != NULL) {
            values[i] = macro->params[i].default_value;
        } else if (macro->params[i].required) {
            minias_set_error(as, "missing-macro-argument:%s:%s:line=%zu",
                             macro->name, macro->params[i].name, line);
            free(copy);
            return false;
        } else {
            values[i] = "";
        }
    }
    if (as->macro_expansion_depth >= 64U) {
        minias_set_error(as, "macro-expansion-depth:%s:line=%zu",
                         macro->name, line);
        free(copy);
        return false;
    }
    if (macro->body_count != 0U) {
        expanded = calloc(macro->body_count, sizeof(*expanded));
        if (expanded == NULL) {
            minias_set_error(as, "out-of-memory:macro-expanded-lines");
            free(copy);
            return false;
        }
    }
    counter = as->macro_expansion_counter++;
    for (i = 0U; i < macro->body_count; ++i) {
        char *current = substitute_macro_counter(as, macro->body[i], counter);
        size_t p;

        if (current == NULL) {
            goto done;
        }
        for (p = 0U; p < macro->param_count; ++p) {
            char *next = substitute_irp_parameter(as,
                                                  current,
                                                  macro->params[p].name,
                                                  values[p]);
            free(current);
            if (next == NULL) {
                goto done;
            }
            current = next;
        }
        expanded[i].text = current;
        expanded[i].line = macro->body_lines[i];
    }
    ++as->macro_expansion_depth;
    ok = process_source_range(as, expanded, 0U, macro->body_count);
    --as->macro_expansion_depth;

done:
    destroy_source_lines(expanded, macro->body_count);
    free(copy);
    return ok;
}

static bool read_macro_block(MiniAs *as,
                             FILE *file,
                             size_t *line_no,
                             MiniAsSourceLine **lines_out,
                             size_t *count_out) {
    char linebuf[65536];
    MiniAsSourceLine *lines = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t depth = 1U;

    while (fgets(linebuf, sizeof(linebuf), file) != NULL) {
        char argument[1024];
        int kind;
        size_t len;

        ++*line_no;
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", *line_no);
            destroy_source_lines(lines, count);
            return false;
        }
        kind = classify_macro_line(linebuf, argument, sizeof(argument));
        if (kind < 0) {
            minias_set_error(as, "out-of-memory:macro-classify");
            destroy_source_lines(lines, count);
            return false;
        }
        if (kind == MINIAS_MACRO_BEGIN) {
            ++depth;
        } else if (kind == MINIAS_MACRO_END) {
            --depth;
            if (depth == 0U) {
                *lines_out = lines;
                *count_out = count;
                return true;
            }
        }
        if (!grow_array((void **)&lines,
                        &capacity,
                        sizeof(*lines),
                        count + 1U)) {
            minias_set_error(as, "out-of-memory:macro-source-lines");
            destroy_source_lines(lines, count);
            return false;
        }
        lines[count].text = minias_strdup(linebuf);
        lines[count].line = *line_no;
        if (lines[count].text == NULL) {
            minias_set_error(as, "out-of-memory:macro-source-line");
            destroy_source_lines(lines, count);
            return false;
        }
        ++count;
    }
    minias_set_error(as, "unterminated-directive:.macro");
    destroy_source_lines(lines, count);
    return false;
}

enum {
    MINIAS_REPEAT_NONE = 0,
    MINIAS_REPEAT_REPT = 1,
    MINIAS_REPEAT_ENDR = 2,
    MINIAS_REPEAT_IRP = 3
};

static void destroy_source_lines(MiniAsSourceLine *lines, size_t count) {
    size_t i;

    for (i = 0U; i < count; ++i) {
        free(lines[i].text);
    }
    free(lines);
}

static int classify_repeat_line(const char *text,
                                char *argument,
                                size_t argument_size) {
    char *copy;
    char *trimmed;
    const char *rest = NULL;
    int kind = MINIAS_REPEAT_NONE;

    if (argument_size != 0U) {
        argument[0] = '\0';
    }
    copy = minias_strdup(text);
    if (copy == NULL) {
        return -1;
    }
    strip_comment(copy);
    trimmed = minias_trim(copy);

    if (strncmp(trimmed, ".rept", 5U) == 0 &&
        (trimmed[5] == '\0' || isspace((unsigned char)trimmed[5]))) {
        rest = trimmed + 5;
        kind = MINIAS_REPEAT_REPT;
    } else if (strncmp(trimmed, ".irp", 4U) == 0 &&
               (trimmed[4] == '\0' ||
                isspace((unsigned char)trimmed[4]))) {
        rest = trimmed + 4;
        kind = MINIAS_REPEAT_IRP;
    } else if (strcmp(trimmed, ".endr") == 0) {
        kind = MINIAS_REPEAT_ENDR;
    }

    if (rest != NULL) {
        while (*rest == ' ' || *rest == '\t') {
            ++rest;
        }
        if (argument_size != 0U) {
            int written = snprintf(argument, argument_size, "%s", rest);
            if (written < 0 || (size_t)written >= argument_size) {
                free(copy);
                return -1;
            }
        }
    }

    free(copy);
    return kind;
}

static char *find_statement_separator(char *text) {
    char quote = '\0';
    bool escaped = false;
    char *p;

    for (p = text; *p != '\0'; ++p) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quote != '\0') {
            if (*p == '\\') {
                escaped = true;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }
        if (*p == ';') {
            return p;
        }
    }
    return NULL;
}

static bool process_source_line(MiniAs *as, const char *source, size_t line) {
    char *copy = minias_strdup(source);
    char *cursor;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:source-line");
        return false;
    }
    strip_comment(copy);
    cursor = copy;
    while (cursor != NULL) {
        char *semicolon = find_statement_separator(cursor);
        if (semicolon != NULL) {
            *semicolon = '\0';
        }
        if (!process_statement(as, cursor, line)) {
            free(copy);
            return false;
        }
        cursor = semicolon == NULL ? NULL : semicolon + 1;
    }
    free(copy);
    return true;
}

static bool repeat_count_value(MiniAs *as,
                               const char *argument,
                               size_t line,
                               uint64_t *count) {
    int64_t value;

    if (*argument == '\0' ||
        !evaluate_absolute_expression(as, argument, &value) ||
        value < 0) {
        minias_set_error(as,
                         "unsupported-expression:.rept:%s:line=%zu",
                         argument,
                         line);
        return false;
    }
    if ((uint64_t)value > UINT32_MAX) {
        minias_set_error(as,
                         "repeat-count-too-large:%s:line=%zu",
                         argument,
                         line);
        return false;
    }
    *count = (uint64_t)value;
    return true;
}

static bool valid_irp_parameter_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;

    if (*p == '\0') {
        return false;
    }
    for (; *p != '\0'; ++p) {
        if (!isalnum(*p) && *p != '_') {
            return false;
        }
    }
    return true;
}

static bool irp_parameter_match(const char *text,
                                const char *name,
                                size_t name_len,
                                size_t *consumed) {
    size_t n = 1U + name_len;
    unsigned char next;

    if (text[0] != '\\' || strncmp(text + 1, name, name_len) != 0) {
        return false;
    }
    next = (unsigned char)text[n];
    if (isalnum(next) || next == '_') {
        return false;
    }
    if (text[n] == '\\' && text[n + 1U] == '(' &&
        text[n + 2U] == ')') {
        n += 3U;
    }
    *consumed = n;
    return true;
}

static char *substitute_irp_parameter(MiniAs *as,
                                      const char *source,
                                      const char *name,
                                      const char *value) {
    size_t source_pos = 0U;
    size_t output_size = 0U;
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    char *output;
    size_t output_pos = 0U;

    while (source[source_pos] != '\0') {
        size_t consumed;
        if (irp_parameter_match(source + source_pos,
                                name,
                                name_len,
                                &consumed)) {
            if (value_len > SIZE_MAX - output_size) {
                minias_set_error(as, "source-size-overflow:.irp");
                return NULL;
            }
            output_size += value_len;
            source_pos += consumed;
        } else {
            if (output_size == SIZE_MAX) {
                minias_set_error(as, "source-size-overflow:.irp");
                return NULL;
            }
            ++output_size;
            ++source_pos;
        }
    }
    if (output_size == SIZE_MAX) {
        minias_set_error(as, "source-size-overflow:.irp");
        return NULL;
    }
    output = malloc(output_size + 1U);
    if (output == NULL) {
        minias_set_error(as, "out-of-memory:irp-substitution");
        return NULL;
    }

    source_pos = 0U;
    while (source[source_pos] != '\0') {
        size_t consumed;
        if (irp_parameter_match(source + source_pos,
                                name,
                                name_len,
                                &consumed)) {
            if (value_len != 0U) {
                memcpy(output + output_pos, value, value_len);
            }
            output_pos += value_len;
            source_pos += consumed;
        } else {
            output[output_pos++] = source[source_pos++];
        }
    }
    output[output_pos] = '\0';
    return output;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end);

static bool process_irp_block(MiniAs *as,
                              MiniAsSourceLine *lines,
                              size_t count,
                              const char *argument,
                              size_t line) {
    char *copy = minias_strdup(argument);
    char *comma;
    char *name;
    char *cursor;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:irp-argument");
        return false;
    }
    comma = strchr(copy, ',');
    if (comma == NULL) {
        minias_set_error(as, "bad-directive:.irp:line=%zu", line);
        free(copy);
        return false;
    }
    *comma = '\0';
    name = minias_trim(copy);
    if (!valid_irp_parameter_name(name)) {
        minias_set_error(as, "bad-directive:.irp:line=%zu", line);
        free(copy);
        return false;
    }

    cursor = comma + 1;
    for (;;) {
        char *next = strchr(cursor, ',');
        char *value;
        MiniAsSourceLine *expanded = NULL;
        size_t i;

        if (next != NULL) {
            *next = '\0';
        }
        value = minias_trim(cursor);
        if (count != 0U) {
            expanded = calloc(count, sizeof(*expanded));
            if (expanded == NULL) {
                minias_set_error(as, "out-of-memory:irp-lines");
                free(copy);
                return false;
            }
        }
        for (i = 0U; i < count; ++i) {
            expanded[i].line = lines[i].line;
            expanded[i].text =
                substitute_irp_parameter(as, lines[i].text, name, value);
            if (expanded[i].text == NULL) {
                destroy_source_lines(expanded, count);
                free(copy);
                return false;
            }
        }
        if (!process_source_range(as, expanded, 0U, count)) {
            destroy_source_lines(expanded, count);
            free(copy);
            return false;
        }
        destroy_source_lines(expanded, count);
        if (next == NULL) {
            break;
        }
        cursor = next + 1;
    }

    free(copy);
    return true;
}

static bool process_source_range(MiniAs *as,
                                 MiniAsSourceLine *lines,
                                 size_t begin,
                                 size_t end) {
    size_t i = begin;

    while (i < end) {
        char macro_argument[1024];
        int macro_kind =
            classify_macro_line(lines[i].text,
                                macro_argument,
                                sizeof(macro_argument));
        char argument[256];
        int kind;

        if (macro_kind < 0) {
            minias_set_error(as, "out-of-memory:macro-classify");
            return false;
        }
        if (macro_kind == MINIAS_MACRO_END) {
            minias_set_error(as,
                             "unmatched-directive:.endm:line=%zu",
                             lines[i].line);
            return false;
        }
        if (macro_kind == MINIAS_MACRO_BEGIN) {
            size_t j = i + 1U;
            size_t depth = 1U;

            while (j < end && depth != 0U) {
                char nested_argument[1024];
                int nested_kind =
                    classify_macro_line(lines[j].text,
                                        nested_argument,
                                        sizeof(nested_argument));
                if (nested_kind < 0) {
                    minias_set_error(as, "out-of-memory:macro-classify");
                    return false;
                }
                if (nested_kind == MINIAS_MACRO_BEGIN) {
                    ++depth;
                } else if (nested_kind == MINIAS_MACRO_END) {
                    --depth;
                }
                if (depth != 0U) {
                    ++j;
                }
            }
            if (depth != 0U || j >= end) {
                minias_set_error(as,
                                 "unterminated-directive:.macro:line=%zu",
                                 lines[i].line);
                return false;
            }
            if (!define_macro(as,
                              macro_argument,
                              lines + i + 1U,
                              j - i - 1U,
                              lines[i].line)) {
                return false;
            }
            i = j + 1U;
            continue;
        }

        kind = classify_repeat_line(lines[i].text,
                                        argument,
                                        sizeof(argument));
        if (kind < 0) {
            minias_set_error(as, "out-of-memory:repeat-classify");
            return false;
        }
        if (kind == MINIAS_REPEAT_ENDR) {
            minias_set_error(as,
                             "unmatched-directive:.endr:line=%zu",
                             lines[i].line);
            return false;
        }
        if (kind == MINIAS_REPEAT_REPT || kind == MINIAS_REPEAT_IRP) {
            size_t j = i + 1U;
            size_t depth = 1U;

            while (j < end && depth != 0U) {
                char nested_argument[256];
                int nested_kind =
                    classify_repeat_line(lines[j].text,
                                         nested_argument,
                                         sizeof(nested_argument));
                if (nested_kind < 0) {
                    minias_set_error(as, "out-of-memory:repeat-classify");
                    return false;
                }
                if (nested_kind == MINIAS_REPEAT_REPT ||
                    nested_kind == MINIAS_REPEAT_IRP) {
                    ++depth;
                } else if (nested_kind == MINIAS_REPEAT_ENDR) {
                    --depth;
                }
                if (depth != 0U) {
                    ++j;
                }
            }
            if (depth != 0U || j >= end) {
                minias_set_error(as,
                                 "unterminated-directive:%s:line=%zu",
                                 kind == MINIAS_REPEAT_IRP ? ".irp" : ".rept",
                                 lines[i].line);
                return false;
            }
            if (kind == MINIAS_REPEAT_REPT) {
                uint64_t repetitions;
                uint64_t iteration;

                if (!repeat_count_value(as,
                                        argument,
                                        lines[i].line,
                                        &repetitions)) {
                    return false;
                }
                for (iteration = 0U; iteration < repetitions; ++iteration) {
                    if (!process_source_range(as, lines, i + 1U, j)) {
                        return false;
                    }
                }
            } else if (!process_irp_block(as,
                                          lines + i + 1U,
                                          j - i - 1U,
                                          argument,
                                          lines[i].line)) {
                return false;
            }
            i = j + 1U;
            continue;
        }
        if (!process_source_line(as, lines[i].text, lines[i].line)) {
            return false;
        }
        ++i;
    }
    return true;
}

static bool read_repeat_block(MiniAs *as,
                              FILE *file,
                              size_t *line_no,
                              int opener_kind,
                              MiniAsSourceLine **lines_out,
                              size_t *count_out) {
    MiniAsSourceLine *lines = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t depth = 1U;
    char linebuf[65536];

    while (fgets(linebuf, sizeof(linebuf), file) != NULL) {
        size_t len;
        char argument[256];
        int kind;

        ++*line_no;
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", *line_no);
            destroy_source_lines(lines, count);
            return false;
        }
        kind = classify_repeat_line(linebuf, argument, sizeof(argument));
        if (kind < 0) {
            minias_set_error(as, "out-of-memory:repeat-classify");
            destroy_source_lines(lines, count);
            return false;
        }
        if (kind == MINIAS_REPEAT_REPT || kind == MINIAS_REPEAT_IRP) {
            ++depth;
        } else if (kind == MINIAS_REPEAT_ENDR) {
            --depth;
            if (depth == 0U) {
                *lines_out = lines;
                *count_out = count;
                return true;
            }
        }
        if (!grow_array((void **)&lines,
                        &capacity,
                        sizeof(*lines),
                        count + 1U)) {
            minias_set_error(as, "out-of-memory:repeat-lines");
            destroy_source_lines(lines, count);
            return false;
        }
        lines[count].text = minias_strdup(linebuf);
        lines[count].line = *line_no;
        if (lines[count].text == NULL) {
            minias_set_error(as, "out-of-memory:repeat-line");
            destroy_source_lines(lines, count);
            return false;
        }
        ++count;
    }

    if (ferror(file)) {
        minias_set_error(as, "input-read:repeat-block");
    } else {
        minias_set_error(as,
                         "unterminated-directive:%s",
                         opener_kind == MINIAS_REPEAT_IRP ? ".irp" : ".rept");
    }
    destroy_source_lines(lines, count);
    return false;
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
        size_t len;
        char argument[256];
        int kind;

        ++line_no;
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", line_no);
            fclose(file);
            return false;
        }

        {
            char macro_argument[1024];
            int macro_kind =
                classify_macro_line(linebuf,
                                    macro_argument,
                                    sizeof(macro_argument));
            if (macro_kind < 0) {
                minias_set_error(as, "out-of-memory:macro-classify");
                fclose(file);
                return false;
            }
            if (macro_kind == MINIAS_MACRO_END) {
                minias_set_error(as,
                                 "unmatched-directive:.endm:line=%zu",
                                 line_no);
                fclose(file);
                return false;
            }
            if (macro_kind == MINIAS_MACRO_BEGIN) {
                MiniAsSourceLine *macro_lines = NULL;
                size_t macro_count = 0U;
                size_t opener_line = line_no;

                if (!read_macro_block(as,
                                      file,
                                      &line_no,
                                      &macro_lines,
                                      &macro_count)) {
                    fclose(file);
                    return false;
                }
                if (!define_macro(as,
                                  macro_argument,
                                  macro_lines,
                                  macro_count,
                                  opener_line)) {
                    destroy_source_lines(macro_lines, macro_count);
                    fclose(file);
                    return false;
                }
                destroy_source_lines(macro_lines, macro_count);
                continue;
            }
        }

        kind = classify_repeat_line(linebuf, argument, sizeof(argument));
        if (kind < 0) {
            minias_set_error(as, "out-of-memory:repeat-classify");
            fclose(file);
            return false;
        }
        if (kind == MINIAS_REPEAT_ENDR) {
            minias_set_error(as,
                             "unmatched-directive:.endr:line=%zu",
                             line_no);
            fclose(file);
            return false;
        }
        if (kind == MINIAS_REPEAT_REPT || kind == MINIAS_REPEAT_IRP) {
            MiniAsSourceLine *lines = NULL;
            size_t count = 0U;
            size_t opener_line = line_no;

            if (!read_repeat_block(as,
                                   file,
                                   &line_no,
                                   kind,
                                   &lines,
                                   &count)) {
                destroy_source_lines(lines, count);
                fclose(file);
                return false;
            }
            if (kind == MINIAS_REPEAT_REPT) {
                uint64_t repetitions;
                uint64_t iteration;

                if (!repeat_count_value(as,
                                        argument,
                                        opener_line,
                                        &repetitions)) {
                    destroy_source_lines(lines, count);
                    fclose(file);
                    return false;
                }
                for (iteration = 0U; iteration < repetitions; ++iteration) {
                    if (!process_source_range(as, lines, 0U, count)) {
                        destroy_source_lines(lines, count);
                        fclose(file);
                        return false;
                    }
                }
            } else if (!process_irp_block(as,
                                          lines,
                                          count,
                                          argument,
                                          opener_line)) {
                destroy_source_lines(lines, count);
                fclose(file);
                return false;
            }
            destroy_source_lines(lines, count);
            continue;
        }
        if (!process_source_line(as, linebuf, line_no)) {
            fclose(file);
            return false;
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
    if (as->cfi_active) {
        minias_set_error(as, "unterminated-directive:.cfi_startproc");
        fclose(file);
        return false;
    }
    fclose(file);
    if (!finalize_subsection_layout(as)) {
        return false;
    }
    return resolve_symbol_aliases(as);
}

typedef struct MiniAsEmitRef {
    MiniAsStmt *stmt;
    size_t order;
} MiniAsEmitRef;

static int compare_emit_refs(const void *lhs_ptr, const void *rhs_ptr) {
    const MiniAsEmitRef *lhs = lhs_ptr;
    const MiniAsEmitRef *rhs = rhs_ptr;

    if (lhs->stmt->section != rhs->stmt->section) {
        return lhs->stmt->section < rhs->stmt->section ? -1 : 1;
    }
    if (lhs->stmt->offset != rhs->stmt->offset) {
        return lhs->stmt->offset < rhs->stmt->offset ? -1 : 1;
    }
    if (lhs->order != rhs->order) {
        return lhs->order < rhs->order ? -1 : 1;
    }
    return 0;
}

bool minias_emit_sections(MiniAs *as) {
    size_t i;
    MiniAsEmitRef *refs = NULL;

    for (i = 0U; i < as->section_count; ++i) {
        as->sections[i].size = 0U;
    }
    if (as->stmt_count != 0U) {
        refs = malloc(as->stmt_count * sizeof(*refs));
        if (refs == NULL) {
            minias_set_error(as, "out-of-memory:emit-order");
            return false;
        }
        for (i = 0U; i < as->stmt_count; ++i) {
            refs[i].stmt = &as->stmts[i];
            refs[i].order = i;
        }
        qsort(refs, as->stmt_count, sizeof(*refs), compare_emit_refs);
    }

    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = refs[i].stmt;
        MiniAsSection *section = &as->sections[(size_t)stmt->section];

        if ((uint64_t)section->size > stmt->offset) {
            minias_set_error(as, "internal-offset:%s:line=%zu", stmt->op, stmt->line);
            free(refs);
            return false;
        }
        if ((uint64_t)section->size < stmt->offset) {
            uint64_t gap = stmt->offset - (uint64_t)section->size;
            if (gap > SIZE_MAX ||
                !minias_section_append_zero(as, stmt->section, (size_t)gap)) {
                free(refs);
                return false;
            }
        }
        if (stmt->kind == MINIAS_STMT_ALIGN) {
            if (!minias_section_append_zero(as, stmt->section, stmt->size)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-align-failed:%s:line=%zu",
                                     stmt->op,
                                     stmt->line);
                }
                free(refs);
                return false;
            }
        } else if (stmt->kind == MINIAS_STMT_DATA) {
            if (!emit_data_stmt(as, stmt)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-data-failed:%s:%s:line=%zu",
                                     stmt->op,
                                     stmt->args,
                                     stmt->line);
                }
                free(refs);
                return false;
            }
        } else if (!minias_riscv_encode(as, stmt)) {
            if (as->error[0] == '\0') {
                minias_set_error(as,
                                 "encode-failed:%s:%s:line=%zu",
                                 stmt->op,
                                 stmt->args,
                                 stmt->line);
            }
            free(refs);
            return false;
        }
    }
    free(refs);

    for (i = 0U; i < as->section_count; ++i) {
        MiniAsSection *section = &as->sections[i];

        if ((uint64_t)section->size > section->layout_size) {
            minias_set_error(as, "internal:section-layout-size:%s", section->name);
            return false;
        }
        if ((uint64_t)section->size < section->layout_size) {
            uint64_t gap = section->layout_size - (uint64_t)section->size;
            if (gap > SIZE_MAX ||
                !minias_section_append_zero(as, (int)i, (size_t)gap)) {
                if (as->error[0] == '\0') {
                    minias_set_error(as,
                                     "emit-section-pad-failed:%s",
                                     section->name);
                }
                return false;
            }
        }
    }
    return true;
}

int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic) {
    MiniAs as;
    int rc = 1;

    minias_init(&as);
    if (as.error[0] == '\0' && minias_parse_file(&as, input_path) &&
        minias_emit_sections(&as) &&
        (elf32 ? minias_write_elf32(&as, output_path)
               : minias_write_elf64(&as, output_path))) {
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

int minias_assemble_file(const char *input_path,
                         const char *output_path,
                         FILE *diagnostic) {
    return minias_assemble_file_class(input_path, output_path, false, diagnostic);
}
