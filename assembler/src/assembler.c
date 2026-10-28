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
    if (elem_size == 0U || next > SIZE_MAX / elem_size) {
        return false;
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

static bool checked_add_i64(int64_t lhs,int64_t rhs,int64_t *out);

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
            if(alias->alias_section_base!=NULL) {
                int target_section=minias_find_section(as,alias->alias_section_base);
                int64_t absolute;
                /* Section-relative GNU aliases are truly ABS only if
                 * the referenced label is defined in that exact section.
                 * The subtraction cancels the section's load address. */
                if(target_section<0 || target->section!=target_section ||
                   value>INT64_MAX ||
                   !checked_add_i64((int64_t)value,
                                    alias->alias_addend,&absolute)) {
                    minias_set_error(as,"invalid-section-relative-set:%s:line=%zu",
                                     alias->name,alias->alias_line);
                    return false;
                }
                alias->defined=true;
                alias->section=MINIAS_SECTION_ABS;
                alias->subsection=0U;
                alias->value=(uint64_t)absolute;
                alias->type=MINIAS_STT_NOTYPE;
                free(alias->alias_section_base);
                alias->alias_section_base=NULL;
                alias->alias_pending=false;
                --remaining;progress=true;
                continue;
            }
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
        if (type != MINIAS_SHT_PROGBITS) {
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

bool minias_add_fixup(MiniAs *as,
                      int section,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      int64_t addend) {
    MiniAsSymbol *symbol;
    MiniAsFixup *fixup;
    size_t symbol_index;
    /* Verify this semantic fixup can be represented by the selected target.
     * ELF relocation numbers are chosen later by the object writer. */
    if (as->target->fixup_relocation_type(kind, width) == 0U) {
        minias_set_error(as,
                         "unsupported-target-fixup:kind=%u:width=%u:symbol=%s",
                         (unsigned)kind, width, symbol_name);
        return false;
    }
    if (section < 0 || (size_t)section >= as->section_count ||
        (width != 2U && width != 4U && width != 8U) ||
        offset > UINT64_MAX - width) {
        minias_set_error(as, "invalid-fixup-range:%s", symbol_name);
        return false;
    }
    symbol = minias_get_symbol(as, symbol_name, true);
    if (symbol == NULL) return false;
    symbol_index = (size_t)(symbol - as->symbols);
    if (!symbol->defined && symbol->bind == MINIAS_STB_LOCAL &&
        !symbol->explicit_local_binding &&
        strncmp(symbol->name, ".L", 2U) != 0)
        symbol->bind = MINIAS_STB_GLOBAL;

    if (!grow_array((void **)&as->fixups,
                    &as->fixup_capacity,
                    sizeof(*as->fixups),
                    as->fixup_count + 1U)) {
        minias_set_error(as, "out-of-memory:fixup");
        return false;
    }
    fixup = &as->fixups[as->fixup_count++];
    fixup->section = section;
    fixup->offset = offset;
    fixup->kind = kind;
    fixup->width = width;
    fixup->symbol_index = symbol_index;
    fixup->addend = addend;
    fixup->prefer_section_symbol = false;
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
    as->target = minias_target_riscv();
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
    as->debug_is_stmt = true;
    (void)ensure_subsection(as, as->current_section, 0U);
}

/* A purged GNU macro owns exactly the resources freed by assembler exit.
 * Keep a single lifetime owner so later redefinitions never retain stale
 * macro parameter/body pointers in the symbol-independent Common layer. */
static void release_macro(MiniAsMacro *macro) {
    size_t i;
    free(macro->name);
    for(i=0U;i<macro->param_count;++i) {
        free(macro->params[i].name);
        free(macro->params[i].default_value);
    }
    free(macro->params);
    for(i=0U;i<macro->body_count;++i)
        free(macro->body[i]);
    free(macro->body);
    free(macro->body_lines);
    memset(macro,0,sizeof(*macro));
}

void minias_destroy(MiniAs *as) {
    size_t i;

    for (i = 0U; i < as->section_count; ++i) {
        free(as->sections[i].name);
        free(as->sections[i].data);
    }
    free(as->sections);
    if(as->target && as->target->destroy_target_state)
        as->target->destroy_target_state(as);
    for (i = 0U; i < as->symbol_count; ++i) {
        free(as->symbols[i].name);
        free(as->symbols[i].alias_section_base);
    }
    free(as->symbols);
    free(as->symbol_slots);
    for (i = 0U; i < as->stmt_count; ++i) {
        free(as->stmts[i].op);
        free(as->stmts[i].args);
        if (as->stmts[i].mcinst != NULL) {
            MiniAsMCInst *mc = as->stmts[i].mcinst;
            size_t k;
            for (k = 0U; k < mc->count; ++k) {
                if (mc->operands[k].kind == MINIAS_MC_SYMBOL)
                    free(mc->operands[k].symbol);
                else if(mc->operands[k].kind == MINIAS_MC_SYMBOL_DIFFERENCE)
                    free(mc->operands[k].difference);
                else if (mc->operands[k].kind == MINIAS_MC_MEMORY) {
                    if (mc->operands[k].memory != NULL) {
                        free(mc->operands[k].memory->symbol);
                        free(mc->operands[k].memory);
                    }
                }
            }
            free(mc);
        }
    }
    free(as->stmts);
    free(as->fixups);
    free(as->literals);
    free(as->subsections);
    free(as->section_stack);
    free(as->numeric_label_counts);
    free(as->conditionals);
    for(i=0U;i<as->macro_count;++i)
        release_macro(&as->macros[i]);
    free(as->macros);
    free(as->cfi_procs);
    free(as->cfi_ops);
    for (i = 0U; i < as->debug_file_capacity; ++i)
        free(as->debug_files[i]);
    free(as->debug_files);
    free(as->debug_locs);
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
    st->target_options = as->target_options;
    if (st->op == NULL || st->args == NULL) {
        minias_set_error(as, "out-of-memory:stmt-text");
        return false;
    }
    ++as->stmt_count;
    if (kind == MINIAS_STMT_INSN &&
        !as->target->parse_mcinst(as, st))
        return false;
    return true;
}

/* Strip GAS C-style block comments before macro/rept classification
 * and statement tokenization. Preserve exact columns and newlines, and
 * never interpret block markers inside quoted strings or line comments.
 * State persists across FILE reads and is shared by nested body readers;
 * no per-ISA comment lexer and no silent unterminated comment acceptance. */
static void strip_gnu_c_comments(MiniAs *as, char *line) {
    char quote='\0';
    bool escaped=false;
    size_t i;
    for(i=0U;line[i]!='\0';++i) {
        if(as->c_comment_active) {
            if(line[i]=='*' && line[i+1U]=='/') {
                line[i++]=' ';line[i]=' ';
                as->c_comment_active=false;
            }else if(line[i]!='\n' && line[i]!='\r')line[i]=' ';
            continue;
        }
        if(escaped){escaped=false;continue;}
        if(quote) {
            if(line[i]=='\\')escaped=true;
            else if(line[i]==quote)quote='\0';
            continue;
        }
        if(line[i]=='"' || line[i]=='\'') {
            quote=line[i];continue;
        }
        if((as->target->comment_prefix=='/' &&
            line[i]=='/' && line[i+1U]=='/') ||
           (as->target->comment_prefix!='/' &&
            line[i]==as->target->comment_prefix))
            return;
        if(line[i]=='/' && line[i+1U]=='*') {
            as->c_comment_active=true;
            line[i++]=' ';line[i]=' ';
        }
    }
}

static void strip_comment(char *line, const MiniAsTargetOps *target) {
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
        if ((target->comment_prefix == '/' && *p == '/' && p[1] == '/') ||
            (target->comment_prefix != '/' && *p == target->comment_prefix)) {
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
    uint32_t type = MINIAS_SHT_PROGBITS;

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

    if (strncmp(name, ".bss", 4U) == 0) {
        type = MINIAS_SHT_NOBITS;
    }

    if (comma != NULL) {
        char *rest = comma + 1;
        char *q = strchr(rest, '"');
        char *type_separator = NULL;

        if (q != NULL) {
            char *flag_end;

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
            flag_end = q;
            type_separator = strchr(flag_end, ',');
        } else {
            type_separator = strchr(rest, ',');
        }

        if (type_separator != NULL) {
            char *kind = minias_trim(type_separator + 1);
            if (*kind == '@' || *kind == '%') {
                ++kind;
            }
            if (strcmp(kind, "note") == 0) {
                type = MINIAS_SHT_NOTE;
            } else if (strcmp(kind, "nobits") == 0) {
                type = MINIAS_SHT_NOBITS;
            } else if (strcmp(kind, "progbits") == 0) {
                type = MINIAS_SHT_PROGBITS;
            }
        }
    }

    if (flags == 0U) {
        if (strncmp(name, ".text", 5U) == 0) {
            flags = MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR;
        } else if (strncmp(name, ".rodata", 7U) == 0 ||
                   strncmp(name, ".note", 5U) == 0) {
            flags = MINIAS_SHF_ALLOC;
        } else if (strncmp(name, ".data", 5U) == 0 ||
                   strncmp(name, ".bss", 4U) == 0) {
            flags = MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        }
    }
    /*
     * RISC-V instructions in executable input subsections must not be
     * placed at byte alignment.  In particular .text.main may follow
     * odd-length .rodata when MiniLD links section-per-function inputs.
     * The .text directive already uses four-byte instruction alignment.
     */
    return switch_section(as,
                          name,
                          type,
                          flags,
                          (flags & MINIAS_SHF_EXECINSTR) != 0U ? 4U : 1U);
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
                /* GNU GAS: a late .local never turns an already-declared
                 * SHN_COMMON global into a local COMMON ELF symbol. */
                if (!(bind == MINIAS_STB_LOCAL && symbol->defined &&
                      symbol->section == MINIAS_SECTION_COMMON)) {
                    symbol->bind = bind;
                    symbol->explicit_local_binding = bind == MINIAS_STB_LOCAL;
                }
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
        symbol->has_size_end_anchor = true;
        symbol->size_end_anchor = as->stmt_count;
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

static bool parse_absolute_logical_or(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value);

static bool parse_absolute_primary(MiniAsAbsoluteExpressionParser *parser,
                                   int64_t *value) {
    const char *start;
    char name[256];
    size_t length;
    MiniAsSymbol *symbol;
    char *end = NULL;
    uint64_t number;

    skip_absolute_expression_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_absolute_logical_or(parser, value)) {
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
        if (symbol == NULL || !symbol->defined ||
            symbol->section == MINIAS_SECTION_COMMON ||
            (symbol->section != MINIAS_SECTION_ABS &&
             symbol->value > (uint64_t)INT64_MAX)) {
            /* ELF COMMON st_value is alignment, not an arithmetic address.
             * GNU rejects its use in a pre-layout absolute expression. */
            return false;
        }
        if (symbol->section != MINIAS_SECTION_ABS) {
            parser->only_absolute_symbols = false;
        }
        *value = (int64_t)symbol->value;
        return true;
    }

    if(!minias_parse_number_literal(parser->cursor,&end,&number))return false;
    parser->cursor=end;
    /* GAS interprets a 64-bit hex mask in arithmetic as a two's-complement
     * value (e.g. subtracting 0xffffffff80000000 adds 0x80000000).
     * Avoid implementation-defined uint64->int64 casts. */
    *value=number>(uint64_t)INT64_MAX?
           -(int64_t)(UINT64_MAX-number)-1:(int64_t)number;
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
    if (*parser->cursor == '~' || *parser->cursor == '!') {
        int64_t operand;
        bool logical=*parser->cursor=='!';
        ++parser->cursor;
        if (!parse_absolute_unary(parser, &operand)) {
            return false;
        }
        *value = logical ? (operand==0?1:0) : ~operand;
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
            /* GNU GAS evaluates signed negative shifts as two's-complement
             * bit operations. C signed << of a negative value is undefined,
             * and rejecting it breaks real Linux extable_type_reg words:
             *   (17 | ((-14) << 16)) + (.Lregnr << 8)
             * Use the canonical unsigned bit pattern, then interpret as
             * signed (same convention as the shared numeric evaluator). */
            uint64_t bits=(uint64_t)result << (unsigned int)shift;
            result=(int64_t)bits;
        } else {
            /* Avoid implementation-dependent negative signed right shift.
             * GNU's >> is arithmetic, explicitly preserving sign bits. */
            unsigned count=(unsigned)shift;
            if(result<0 && count!=0U) {
                uint64_t bits=(uint64_t)result;
                bits=(bits>>count)|(UINT64_MAX<<(64U-count));
                result=(int64_t)bits;
            } else result>>=count;
        }
    }
    *value = result;
    return true;
}

/* GNU GAS absolute comparisons produce -1 for true and 0 for false.
 * These are arithmetic expression operators, not only .if directives.
 * In particular Linux hand-written .fill uses -((symA-symB)>0).
 * Build comparison precedence once in Common, leaving Target expression
 * syntax and deferred symbol fixups independent. */
static bool parse_absolute_relational(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_shift(parser,&lhs))return false;
    for(;;) {
        bool lt,gt,inclusive;
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if((parser->cursor[0]!='<' && parser->cursor[0]!='>') ||
           parser->cursor[1]==parser->cursor[0])break;
        lt=parser->cursor[0]=='<';gt=!lt;
        inclusive=parser->cursor[1]=='=';
        parser->cursor+=inclusive?2U:1U;
        if(!parse_absolute_shift(parser,&rhs))return false;
        lhs=((lt && (inclusive?lhs<=rhs:lhs<rhs)) ||
             (gt && (inclusive?lhs>=rhs:lhs>rhs)))?-1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_equality(MiniAsAbsoluteExpressionParser *parser,
                                    int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_relational(parser,&lhs))return false;
    for(;;) {
        bool different;
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"==",2U)==0)different=false;
        else if(strncmp(parser->cursor,"!=",2U)==0)different=true;
        else break;
        parser->cursor+=2U;
        if(!parse_absolute_relational(parser,&rhs))return false;
        lhs=((lhs!=rhs)==different)?-1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_and(MiniAsAbsoluteExpressionParser *parser,
                               int64_t *value) {
    int64_t result;

    if (!parse_absolute_equality(parser, &result)) {
        return false;
    }
    for (;;) {
        int64_t rhs;

        skip_absolute_expression_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            break;
        }
        ++parser->cursor;
        if (!parse_absolute_equality(parser, &rhs)) {
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

/* GNU GAS boolean operators are a separate, lower-precedence layer
 * after bitwise |/^. Unlike comparisons (-1 for true), GNU '||',
 * '&&' and unary '!' produce 1/0. Parse both operands and retain the
 * existing checked absolute arithmetic; never silently accept an
 * unresolvable symbol just because the LHS is true. */
static bool parse_absolute_logical_and(MiniAsAbsoluteExpressionParser *parser,
                                      int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_or(parser,&lhs))return false;
    for(;;) {
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"&&",2U))break;
        parser->cursor+=2U;
        if(!parse_absolute_or(parser,&rhs))return false;
        lhs=(lhs!=0 && rhs!=0)?1:0;
    }
    *value=lhs;return true;
}

static bool parse_absolute_logical_or(MiniAsAbsoluteExpressionParser *parser,
                                     int64_t *value) {
    int64_t lhs;
    if(!parse_absolute_logical_and(parser,&lhs))return false;
    for(;;) {
        int64_t rhs;
        skip_absolute_expression_space(parser);
        if(strncmp(parser->cursor,"||",2U))break;
        parser->cursor+=2U;
        if(!parse_absolute_logical_and(parser,&rhs))return false;
        lhs=(lhs!=0 || rhs!=0)?1:0;
    }
    *value=lhs;return true;
}

static bool evaluate_absolute_expression(MiniAs *as,
                                         const char *text,
                                         int64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;
    uint64_t unsigned_value;

    /* GNU supports full-precision unsigned constants in .set/.equ and
     * conditional expressions. Resolve purely numeric, final in-range
     * expressions before the signed symbolic path; never coerce
     * unresolved symbols or relocations into an absolute value. */
    if (minias_eval_u64_expression(text, &unsigned_value) &&
        unsigned_value <= (uint64_t)INT64_MAX) {
        *value = (int64_t)unsigned_value;
        return true;
    }
    if(as->expr_dot_override)dot=as->expr_dot_value;
    else if(!current_location(as,&dot))return false;
    if(dot>(uint64_t)INT64_MAX)return false;
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, value)) {
        return false;
    }
    skip_absolute_expression_space(&parser);
    return *parser.cursor == '\0';
}

bool minias_evaluate_absolute_operand(MiniAs *as,const char *text,
                                      int64_t *value) {
    return text && value && evaluate_absolute_expression(as,text,value);
}

/* GNU GAS accepts:
 *   b external + (1b - 0b + (2 * 4))
 * after Common numeric-label rewriting.  Only the local-label subtraction
 * is folded: the base symbol remains a typed relocation with an exact
 * addend, not an inferred displacement or a parallel Target evaluator.
 * Both labels must ALREADY be defined in the SAME subsection, where
 * their difference is invariant under later section layout. */
/* A GNU expression such as (identity_mapped - relocate_kernel) is
 * absolute only AFTER both label anchors exist and subsection layout is
 * finalized. Preserve the two typed symbols throughout parse and measure:
 * parsing a forward symbol as zero or inventing a relocation is wrong.
 * Common owns syntax and final same-section semantics for all targets. */
bool minias_parse_symbol_difference(const char *expression,
                                    MiniAsSymbolDifference *out) {
    char *copy,*value,*minus;
    size_t n,i;
    unsigned level;
    bool wrapping;
    bool ok=false;
    if(!expression || !out)return false;
    copy=minias_strdup(expression);
    if(!copy)return false;
    value=minias_trim(copy);
    /* Strip whole-expression parentheses, never partial subexpressions. */
    for(;;) {
        n=strlen(value);
        if(n<3U || value[0]!='(' || value[n-1U]!=')')break;
        level=0U;wrapping=true;
        for(i=0U;i<n;++i) {
            if(value[i]=='(')++level;
            else if(value[i]==')') {
                if(level==0U){wrapping=false;break;}
                --level;
                if(level==0U && i!=n-1U){wrapping=false;break;}
            }
        }
        if(!wrapping || level!=0U)break;
        value[n-1U]='\0';
        value=minias_trim(value+1U);
    }
    minus=strchr(value,'-');
    if(minus && minus!=value) {
        *minus++='\0';
        if(minias_parse_symbol_addend(minias_trim(value),&out->positive) &&
           minias_parse_symbol_addend(minias_trim(minus),&out->negative))
            ok=true;
    }
    free(copy);
    return ok;
}

bool minias_resolve_symbol_difference(MiniAs *as,
                                      const MiniAsSymbolDifference *pair,
                                      int64_t *value) {
    const MiniAsSymbol *positive,*negative;
    int64_t first,second,delta;
    if(!as || !pair || !value)return false;
    positive=minias_get_symbol(as,pair->positive.name,false);
    negative=minias_get_symbol(as,pair->negative.name,false);
    if(!positive || !negative ||
       !positive->defined || !negative->defined ||
       positive->section<0 || positive->section!=negative->section ||
       positive->value>INT64_MAX || negative->value>INT64_MAX)
        return false;
    first=(int64_t)positive->value;
    second=(int64_t)negative->value;
    if(!checked_add_i64(first,pair->positive.addend,&first) ||
       !checked_add_i64(second,pair->negative.addend,&second) ||
       !checked_sub_i64(first,second,&delta))return false;
    *value=delta;
    return true;
}

bool minias_fold_local_difference_addend(MiniAs *as,const char *expression,
                                         MiniAsSymbolExpr *out) {
    char *copy=minias_strdup(expression),*plus,*group,*minus,*local_extra;
    MiniAsSymbolExpr base,left,right;
    MiniAsSymbol *l,*r;
    int64_t diff,add=0,final;
    bool ok=false;
    size_t n;
    if(!copy)return false;
    plus=strchr(copy,'+');
    if(!plus)goto done;
    *plus++='\0';
    if(!minias_parse_symbol_addend(minias_trim(copy),&base))goto done;
    group=minias_trim(plus);
    n=strlen(group);
    if(n<5U || group[0]!='(' || group[n-1U]!=')')goto done;
    group[n-1U]='\0';
    group=minias_trim(group+1U);
    minus=strchr(group,'-');
    if(!minus)goto done;
    *minus++='\0';
    if(!minias_parse_symbol_addend(minias_trim(group),&left) ||
       left.addend!=0)goto done;
    local_extra=strchr(minus,'+');
    if(local_extra) {
        *local_extra++='\0';
        if(!evaluate_absolute_expression(as,minias_trim(local_extra),&add))
            goto done;
    }
    if(!minias_parse_symbol_addend(minias_trim(minus),&right) ||
       right.addend!=0)goto done;
    l=minias_get_symbol(as,left.name,false);
    r=minias_get_symbol(as,right.name,false);
    if(!l || !r || !l->defined || !r->defined ||
       l->section<0 || l->section!=r->section ||
       l->subsection!=r->subsection ||
       l->value>INT64_MAX || r->value>INT64_MAX)goto done;
    diff=(int64_t)l->value-(int64_t)r->value;
    if((add>0 && diff>INT64_MAX-add) ||
       (add<0 && diff<INT64_MIN-add))goto done;
    diff+=add;
    if((diff>0 && base.addend>INT64_MAX-diff) ||
       (diff<0 && base.addend<INT64_MIN-diff))goto done;
    final=base.addend+diff;
    *out=base;
    out->addend=final;
    as->layout_sensitive=true;
    ok=true;
done:
    free(copy);
    return ok;
}

bool minias_parse_absolute_data_bits(MiniAs *as,
                                     const char *text,
                                     uint64_t *value) {
    MiniAsAbsoluteExpressionParser parser;
    uint64_t dot;
    int64_t signed_value;

    if (parse_data_integer_bits(text, value)) {
        return true;
    }
    /* Common unsigned expression grammar supports GNU C-preprocessed
     * 64-bit constants including 0x9000...ULL, shifts, masks and brackets.
     * This numeric-only fast path cannot resolve unknown symbols and thus
     * cannot fabricate relocations or pre-layout symbol differences. */
    if(minias_eval_u64_expression(text,value))return true;
    if (!current_location(as, &dot) || dot > (uint64_t)INT64_MAX) {
        return false;
    }
    parser.as = as;
    parser.cursor = text;
    parser.dot = (int64_t)dot;
    parser.only_absolute_symbols = true;
    if (!parse_absolute_logical_or(&parser, &signed_value)) {
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
    /* Signed absolute ELF .equ is legal: -1 is stored in uint64_t
     * symbol storage as two's-complement bits and interpreted as i64 by
     * Common's signed expression evaluator. Only relocatable values
     * require section-specific nonnegative address constraints. */
    if (*name == '\0' || *expression == '\0' ||
        !evaluate_absolute_expression(as, expression, &value)) {
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
                location_constant = isdigit((unsigned char)*tail) != 0;
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

    /* GNU .set is mutable, unlike .equ. Evaluate an already-defined
     * absolute symbol RHS at the *current* statement position, including
     * self-referential .set counter, counter+1 in .irp/macro expansion.
     * Preserve relocatable symbol aliases via the existing fallthrough. */
    {
        int64_t value;
        if(evaluate_absolute_expression(as,expression,&value)) {
            alias=minias_get_symbol(as,name,true);
            if(!alias)return false;
            if(alias->alias_pending ||
               (alias->defined &&
                (alias->section!=MINIAS_SECTION_ABS ||
                 !alias->mutable_absolute_set))) {
                minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
                return false;
            }
            alias->defined=true;
            alias->section=MINIAS_SECTION_ABS;
            alias->subsection=0U;
            alias->value=(uint64_t)value;
            alias->mutable_absolute_set=true;
            as->has_mutable_absolute_sets=true;
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

    /* GNU ARM64 tramp_alias:
     * .set NAME, TRAMP_VALIAS + tramp_exit - .entry.tramp.text
     * Section-base subtraction turns a forward section symbol into a
     * later absolute constant; the macro often runs before tramp_exit is
     * defined. Preserve a typed pending symbol alias, never fabricate a
     * section-relative numeric address or ignore the .set directive. */
    {
        char *copy=minias_strdup(expression);
        if(!copy){minias_set_error(as,"out-of-memory:section-set");return false;}
        char *sum=strchr(copy,'+');
        char *minus=sum?strrchr(sum+1U,'-'):NULL;
        MiniAsSymbolExpr base;
        int64_t number;
        bool valid=false;
        if(sum && minus && minus>sum+1U) {
            *sum++='\0';*minus++='\0';
            char *section_name=minias_trim(minus);
            char *target_name=minias_trim(sum);
            if(*section_name=='.' &&
               minias_eval_i64_expression(minias_trim(copy),&number) &&
               minias_parse_symbol_addend(target_name,&base) &&
               base.addend==0) {
                MiniAsSymbol *ref=minias_get_symbol(as,base.name,true);
                if(ref!=NULL) {
                    size_t ref_index=(size_t)(ref-as->symbols);
                    MiniAsSymbol *result=minias_get_symbol(as,name,true);
                    if(result && !result->defined && !result->alias_pending) {
                        result->alias_section_base=minias_strdup(section_name);
                        if(result->alias_section_base!=NULL) {
                            result->alias_pending=true;
                            result->alias_target_index=ref_index;
                            result->alias_addend=number;
                            result->alias_line=line;
                            valid=true;
                        }
                    }
                }
            }
        }
        free(copy);
        if(valid)return true;
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

/* A new ISA may request a pool address without teaching Core the ISA
 * encoding or introducing a parallel ELF writer.  Records are grouped by
 * SECTION+SUBSECTION and deduped only until the next .ltorg flush. */
bool minias_queue_literal(MiniAs *as,const MiniAsSymbolExpr *expression,
                          unsigned width,MiniAsSymbolExpr *pool_label) {
    MiniAsLiteral *entry;
    size_t i;
    int written;
    if(!as->target->supports_literal_pools || as->current_section<0 ||
       !expression || !pool_label || (width!=4U && width!=8U)) {
        minias_set_error(as,"unsupported-target-literal-pool");return false;
    }
    for(i=0U;i<as->literal_count;++i) {
        entry=&as->literals[i];
        if(!entry->emitted && entry->section==as->current_section &&
           entry->subsection==as->current_subsection &&
           entry->width==width &&
           entry->expression.addend==expression->addend &&
           !strcmp(entry->expression.name,expression->name)) {
            written=snprintf(pool_label->name,sizeof(pool_label->name),
                             "%s",entry->label);
            if(written<0 || (size_t)written>=sizeof(pool_label->name))
                return false;
            pool_label->addend=0;return true;
        }
    }
    if(as->literal_count==SIZE_MAX ||
       !grow_array((void **)&as->literals,&as->literal_capacity,
                   sizeof(*as->literals),as->literal_count+1U)) {
        minias_set_error(as,"out-of-memory:literal-pool");return false;
    }
    entry=&as->literals[as->literal_count];
    memset(entry,0,sizeof(*entry));
    entry->section=as->current_section;
    entry->subsection=as->current_subsection;
    entry->width=width;entry->expression=*expression;
    written=snprintf(entry->label,sizeof(entry->label),
                     ".Lminias_literal_%zu",as->literal_count);
    if(written<0 || (size_t)written>=sizeof(entry->label)) {
        minias_set_error(as,"literal-label-overflow");return false;
    }
    ++as->literal_count;
    written=snprintf(pool_label->name,sizeof(pool_label->name),
                     "%s",entry->label);
    if(written<0 || (size_t)written>=sizeof(pool_label->name))return false;
    pool_label->addend=0;
    return true;
}

/* GNU .p2align/.balign/.align alignment[,fill[,max_skip]].
 * Target owns NOP encoding, Common owns fragment length and optional fill. */
static bool parse_align_args(MiniAs *as,const char *op,const char *args,
                             uint64_t *align,bool *has_fill,uint8_t *fill,
                             bool *has_max,uint64_t *max_skip){
    char *copy=minias_strdup(args),*cursor,*comma,*parts[3];
    unsigned n=0U;
    int64_t value;
    if(!copy)return false;
    cursor=copy;
    for(;;) {
        if(n>=3U){free(copy);return false;}
        parts[n++]=minias_trim(cursor);
        comma=strchr(cursor,',');
        if(!comma)break;
        *comma='\0';cursor=comma+1U;
    }
    if(!*parts[0] ||
       !evaluate_absolute_expression(as,parts[0],&value) ||
       value<0){free(copy);return false;}
    if(!strcmp(op,".p2align") ||
       (!strcmp(op,".align") && !as->target->align_directive_bytes)){
        if(value>=63){free(copy);return false;}
        *align=UINT64_C(1)<<(unsigned)value;
    }else *align=(uint64_t)value;
    if(!*align || (*align&(*align-1U)) || *align>UINT32_MAX){
        free(copy);return false;
    }
    *has_fill=false;*fill=0U;*has_max=false;*max_skip=0U;
    if(n>=2U && *parts[1]){
        if(!evaluate_absolute_expression(as,parts[1],&value) ||
           value<-128 || value>255){free(copy);return false;}
        *has_fill=true;*fill=(uint8_t)value;
    }
    if(n>=3U && *parts[2]){
        if(!evaluate_absolute_expression(as,parts[2],&value) ||
           value<0){free(copy);return false;}
        *has_max=true;*max_skip=(uint64_t)value;
    }
    free(copy);return true;
}

static bool handle_align(MiniAs *as,const char *op,char *args,size_t line){
    uint64_t alignment,current,pad,max_skip;
    bool has_fill,has_max;
    uint8_t fill;
    MiniAsStmt *stmt;
    if(!parse_align_args(as,op,args,&alignment,&has_fill,&fill,
                         &has_max,&max_skip)){
        minias_set_error(as,"unsupported-alignment:%s:line=%zu",op,line);
        return false;
    }
    if(!current_location(as,&current))return false;
    pad=(alignment-(current&(alignment-1U)))&(alignment-1U);
    if(has_max && pad>max_skip)pad=0U;
    if(pad>UINT32_MAX)return false;
    if(!add_stmt(as,MINIAS_STMT_ALIGN,op,"",line,(uint32_t)pad,alignment))
        return false;
    stmt=&as->stmts[as->stmt_count-1U];
    stmt->align_has_fill=has_fill;stmt->align_fill=fill;
    stmt->align_has_max_skip=has_max;stmt->align_max_skip=max_skip;
    if(!advance_current_location(as,pad))return false;
    if(alignment>as->sections[(size_t)as->current_section].align)
        as->sections[(size_t)as->current_section].align=alignment;
    return true;
}

static unsigned int data_width(const MiniAs *as, const char *op) {
    if (strcmp(op, ".byte") == 0) {
        return 1U;
    }
    if (strcmp(op, ".half") == 0 || strcmp(op, ".short") == 0 ||
        strcmp(op, ".hword") == 0 || strcmp(op, ".value") == 0 ||
        strcmp(op, ".2byte") == 0) {
        return 2U;
    }
    if (!strcmp(op,".inst")) return as->target->inst_directive_width;
    if (strcmp(op, ".word") == 0)
        return as->target->word_directive_width == 0U ?
            4U : as->target->word_directive_width;
    if (strcmp(op, ".long") == 0 || strcmp(op, ".4byte") == 0) {
        return 4U;
    }
    if (strcmp(op, ".dword") == 0 || strcmp(op, ".quad") == 0 ||
        strcmp(op, ".xword") == 0 || strcmp(op, ".8byte") == 0) {
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

/* GAS permits parentheses around a data relocation symbol, including
 * nested (symbol+addend) groups. Normalize a private copy only for data
 * relocation parsing: never mutate the stored statement or bypass the
 * Common typed Fixup path. The dominant plain-symbol path stays alloc-free. */
/* One relocatable symbol plus an otherwise absolute GNU arithmetic tail.
 * Resolve .set counters at their lexical DATA statement, not at final
 * emit, and retain an ordinary typed SymbolExpr/ELF addend. */
static bool parse_data_symbol_absolute_tail(MiniAs *as,const char *text,
                                             MiniAsSymbolExpr *expr) {
    const char *p=text,*begin;
    char name[256];
    size_t len;
    int64_t addend;
    while(isspace((unsigned char)*p))++p;
    if(!absolute_symbol_start(*p))return false;
    begin=p++;
    while(absolute_symbol_continue(*p))++p;
    len=(size_t)(p-begin);
    if(len==0U || len>=sizeof(name))return false;
    memcpy(name,begin,len);name[len]='\0';
    while(isspace((unsigned char)*p))++p;
    if((*p!='+' && *p!='-') ||
       !minias_parse_symbol_addend(name,expr) ||
       !evaluate_absolute_expression(as,p,&addend))return false;
    expr->addend=addend;
    return true;
}

static bool parse_data_symbol_addend(const char *text,
                                     MiniAsSymbolExpr *expr) {
    char *copy;
    bool valid;
    if(*text!='(')return minias_parse_symbol_addend(text,expr);
    copy=minias_strdup(text);
    if(!copy)return false;
    valid=minias_parse_symbol_addend(strip_outer_parens(copy),expr);
    free(copy);
    return valid;
}

/* GAS conditional expressions share the Common absolute arithmetic and
 * symbol resolver. A separate numeric-only parser rejected macro constants
 * such as .if docfi=0 and prematurely fragmented Linux target assembly.
 * Operators are recognized only at top-level, never in shifts or parens. */
static bool evaluate_if_expression(MiniAs *as,const char *text,bool *result) {
    char *copy,*p,*lhs,*rhs=NULL;
    const char *compare=NULL;
    char op='\0';
    unsigned depth=0U;
    unsigned width=0U;
    int64_t left,right;
    if(!text || !result)return false;
    copy=minias_strdup(text);
    if(!copy)return false;
    lhs=minias_trim(copy);
    /* Delegate logical expressions to the one Common precedence parser
     * rather than splitting an inner '==' away from its '||' RHS. */
    if(strstr(lhs,"||") || strstr(lhs,"&&")) {
        bool ok=evaluate_absolute_expression(as,lhs,&left);
        free(copy);
        if(ok)*result=left!=0;
        return ok;
    }
    for(p=lhs;*p;++p) {
        if(*p=='('){++depth;continue;}
        if(*p==')') {
            if(depth==0U){free(copy);return false;}
            --depth;continue;
        }
        if(depth)continue;
        if((p[0]=='<' && p[1]=='<') ||
           (p[0]=='>' && p[1]=='>')) {++p;continue;}
        if((p[0]=='=' && p[1]=='=') ||
           (p[0]=='!' && p[1]=='=') ||
           (p[0]=='<' && p[1]=='=') ||
           (p[0]=='>' && p[1]=='=')) {
            compare=p;width=2U;break;
        }
        if(*p=='=' || *p=='<' || *p=='>') {
            compare=p;width=1U;break;
        }
    }
    if(depth!=0U){free(copy);return false;}
    if(compare) {
        p=(char *)compare;
        rhs=p+width;
        op=*p;
        *p='\0';
        lhs=minias_trim(lhs);rhs=minias_trim(rhs);
        if(*lhs=='\0' || *rhs=='\0' ||
           !evaluate_absolute_expression(as,lhs,&left) ||
           !evaluate_absolute_expression(as,rhs,&right)) {
            free(copy);return false;
        }
        switch(op) {
            case '=':*result=left==right;break;
            case '!':*result=left!=right;break;
            case '<':*result=width==2U?left<=right:left<right;break;
            case '>':*result=width==2U?left>=right:left>right;break;
            default:free(copy);return false;
        }
    } else {
        if(!evaluate_absolute_expression(as,lhs,&left)) {
            free(copy);return false;
        }
        *result=left!=0;
    }
    free(copy);return true;
}

static bool push_conditional_value(MiniAs *as,bool condition_true) {
    MiniAsConditional *conditional;
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

/* Reuse the sole Common numeric-label resolver in GAS conditionals:
 * backward 661b/662b references have exact fragment-backed values. */
static bool rewrite_numeric_local_refs(MiniAs *,const char *,
                                       char **,size_t);
static bool evaluate_conditional_with_labels(MiniAs *as,const char *args,
                                              size_t line,bool *selected) {
    char *expanded=NULL;
    bool valid;
    if(!rewrite_numeric_local_refs(as,args,&expanded,line))return false;
    valid=evaluate_if_expression(as,expanded,selected) ||
          (as->target->evaluate_target_if &&
           as->target->evaluate_target_if(expanded,selected));
    free(expanded);return valid;
}

static bool push_conditional(MiniAs *as, const char *args, size_t line) {
    bool condition_true;
    /* False parent: keep the nested conditional frame, but GNU GAS
     * does not resolve symbols in branches that cannot be reached. */
    if(!as->conditional_active)
        return push_conditional_value(as,false);
    if (!evaluate_conditional_with_labels(as,args,line,&condition_true)) {
        minias_set_error(as, "unsupported-expression:.if:%s:line=%zu", args, line);
        return false;
    }
    return push_conditional_value(as,condition_true);
}

/* All numeric GNU .if* variants have the same Common conditional frame:
 * skip unreachable parents without evaluating unresolved expressions. */
static bool push_conditional_numeric(MiniAs *as,const char *args,
                                     const char *directive,size_t line) {
    int64_t value;
    bool active;
    if(!as->conditional_active)return push_conditional_value(as,false);
    if(!evaluate_absolute_expression(as,args,&value)) {
        minias_set_error(as,"unsupported-expression:%s:%s:line=%zu",
                         directive,args,line);
        return false;
    }
    active=!strcmp(directive,".ifeq")?value==0:
           !strcmp(directive,".ifne")?value!=0:
           !strcmp(directive,".iflt")?value<0:
           !strcmp(directive,".ifle")?value<=0:
           !strcmp(directive,".ifgt")?value>0:value>=0;
    return push_conditional_value(as,active);
}

/* GNU GAS .ifdef/.ifndef inspect the CURRENT defined symbol table,
 * not relocation references, global declarations or future labels.
 * Share the exact same conditional stack as .if/.ifc/.elseif and do
 * not create symbol records while checking existence. */
static bool push_conditional_defined(MiniAs *as,const char *args,
                                      bool negate,size_t line) {
    const char *p=args;
    MiniAsSymbol *symbol;
    bool defined;
    if(!as->conditional_active)
        return push_conditional_value(as,false);
    if(!(*p=='_' || *p=='.' || *p=='$' ||
         isalpha((unsigned char)*p))) {
        minias_set_error(as,"bad-directive:.ifdef:line=%zu",line);
        return false;
    }
    for(++p;*p;++p) {
        if(!(*p=='_' || *p=='.' || *p=='$' ||
             isalnum((unsigned char)*p))) {
            minias_set_error(as,"bad-directive:.ifdef:line=%zu",line);
            return false;
        }
    }
    symbol=minias_get_symbol(as,args,false);
    defined=symbol && (symbol->defined || symbol->alias_pending);
    return push_conditional_value(as,negate?!defined:defined);
}

/* Shared GAS .ifc/.ifnc conditional text: split on a comma outside
 * quotes, strip paired quote delimiters, then compare exact byte strings.
 * Expansion of macro arguments already happens upstream.  No target
 * ISA-specific conditional parser or new conditional stack is needed. */
/* GNU GAS .ifc/.ifnc compares trimmed *literal text*: a missing side
 * is a valid empty string, a quoted comma is still a separator, and a
 * comma in the second operand belongs to that operand. Macro expansion
 * happens before this common conditional stack is evaluated. */
static bool push_conditional_strings(MiniAs *as,const char *args,
                                     bool unequal,bool double_quoted,
                                     size_t line) {
    char *copy=minias_strdup(args),*lhs,*rhs,*comma;
    bool equal;
    if(!copy){minias_set_error(as,"out-of-memory:.ifc");return false;}
    if(double_quoted) {
        /* GAS .ifeqs/.ifnes require exactly two double-quoted strings.
         * A comma inside a quoted operand is data, not the delimiter.
         * Keep this on the same Common conditional stack as .ifc/.ifnc. */
        char *end;
        lhs=minias_trim(copy);
        if(*lhs!='"')goto malformed;
        ++lhs;
        end=strchr(lhs,'"');
        if(!end)goto malformed;
        *end++='\0';
        end=minias_trim(end);
        if(*end!=',')goto malformed;
        rhs=minias_trim(end+1);
        if(*rhs!='"')goto malformed;
        ++rhs;
        end=strchr(rhs,'"');
        if(!end)goto malformed;
        *end++='\0';
        if(*minias_trim(end)!='\0')goto malformed;
    }else {
        /* Existing .ifc/.ifnc first-comma semantics are intentional. */
        comma=strchr(copy,',');
        if(!comma)goto malformed;
        *comma='\0';
        lhs=minias_trim(copy);
        rhs=minias_trim(comma+1);
    }
    equal=strcmp(lhs,rhs)==0;
    free(copy);
    return push_conditional_value(as,unequal?!equal:equal);
malformed:
    minias_set_error(as,"bad-directive:%s:line=%zu",
                     double_quoted?".ifeqs":".ifc",line);
    free(copy);return false;
}

/* GAS .elseif shares the same frame as .if/.else. Once an
 * earlier arm wins, do not evaluate later arms: symbols there need
 * not exist, and ignored nested branches must not leak statements. */
static bool elseif_conditional(MiniAs *as,const char *args,size_t line) {
    MiniAsConditional *conditional;
    bool condition=false;
    if(as->conditional_count==0U) {
        minias_set_error(as,"unmatched-directive:.elseif:line=%zu",line);
        return false;
    }
    conditional=&as->conditionals[as->conditional_count-1U];
    if(conditional->else_seen) {
        minias_set_error(as,"elseif-after-else:line=%zu",line);
        return false;
    }
    if(conditional->parent_active && !conditional->condition_true) {
        if(!evaluate_conditional_with_labels(as,args,line,&condition)) {
            minias_set_error(as,"unsupported-expression:.elseif:%s:line=%zu",
                             args,line);
            return false;
        }
    }
    as->conditional_active=conditional->parent_active &&
                           !conditional->condition_true && condition;
    conditional->condition_true|=condition;
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
    uint64_t trailing_addend = 0U;

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
        /* GNU accepts `symbol - . + addend` as a single PC-relative
         * relocation. Keep the semantic symbol expression typed;
         * do not have ELF targets parse or rewrite source strings. */
        char *suffix_tail=suffix;
        if(*suffix_tail++!='.') {
            free(copy);return false;
        }
        while(isspace((unsigned char)*suffix_tail))++suffix_tail;
        if(*suffix_tail++!='+') {
            free(copy);return false;
        }
        suffix_tail=minias_trim(suffix_tail);
        if(!parse_u64(suffix_tail,&trailing_addend) ||
           trailing_addend>INT64_MAX) {
            free(copy);return false;
        }
    }
    lhs = strip_outer_parens(minias_trim(normalized));
    ok = minias_parse_symbol_addend(lhs, expr);
    if(ok && trailing_addend!=0U) {
        if(expr->addend>INT64_MAX-(int64_t)trailing_addend)
            ok=false;
        else expr->addend+=(int64_t)trailing_addend;
    }
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
    bool rhs_parenthesized;
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
    rhs = minias_trim(minus + 1);
    /* GNU expression precedence: A-B+11 means (A-B)+11, whereas
     * A-(B+11) means A-B-11. Preserve the grouping *before* removing
     * enclosing parentheses from the right-hand symbol expression. */
    rhs_parenthesized=rhs[0]=='(' &&
                       rhs[strlen(rhs)-1U]==')';
    rhs = strip_outer_parens(rhs);
    if (strcmp(rhs, ".") == 0) {
        free(copy);
        return false;
    }
    ok = minias_parse_symbol_addend(lhs, lhs_expr) &&
         minias_parse_symbol_addend(rhs, rhs_expr);
    if(ok && !rhs_parenthesized && rhs_expr->addend!=0) {
        int64_t extra=rhs_expr->addend;
        if((extra>0 && lhs_expr->addend>INT64_MAX-extra) ||
           (extra<0 && lhs_expr->addend<INT64_MIN-extra))
            ok=false;
        else {
            lhs_expr->addend+=extra;
            rhs_expr->addend=0;
        }
    }
    free(copy);
    return ok;
}

/* GNU code-relative data tables use (S1-S2)/4 in .byte/.2byte,
 * including forward local labels from ARM64 exception tables. This is
 * Common section arithmetic, not an ISA-specific relocation opcode. */
static bool parse_symbol_difference_divisor(const char *text,
        MiniAsSymbolExpr *lhs,MiniAsSymbolExpr *rhs,uint64_t *divisor,
        bool *arithmetic_shift) {
    char *copy=minias_strdup(text),*p,*operator_at=NULL,*normalized;
    unsigned depth=0U;
    bool ok=false,shift=false;
    uint64_t n;
    if(!copy)return false;
    /* GNU parenthesizes the ENTIRE shifted expression:
     * ((end-start) >> 5). Remove only whole-expression wrapping
     * parentheses BEFORE locating top-level >> or division; otherwise
     * depth never reaches zero and the operator is incorrectly hidden. */
    normalized=strip_outer_parens(copy);
    for(p=normalized;*p;++p) {
        if(*p=='(')++depth;
        else if(*p==')') {
            if(!depth)goto done;
            --depth;
        }else if(depth==0U && *p=='/') {
            if(operator_at)goto done;
            operator_at=p;
        }else if(depth==0U && p[0]=='>' && p[1]=='>') {
            if(operator_at)goto done;
            operator_at=p;shift=true;++p;
        }
    }
    if(depth || !operator_at)goto done;
    *operator_at='\0';
    if(!parse_u64(minias_trim(operator_at+(shift?2U:1U)),&n))
        goto done;
    /* GAS section-symbol differences use unsigned 64-bit logical >>5:
     * -33 >> 5 = 0x07fffffffffffffe, not signed -2 and not -1.
     * Reject values that cannot fit in the destination .word/.long. */
    if(shift) {
        if(n>=63U)goto done;
        *divisor=UINT64_C(1)<<n;
    }else {
        if(n==0U || n>(uint64_t)INT64_MAX)goto done;
        *divisor=n;
    }
    if(parse_symbol_difference(strip_outer_parens(minias_trim(normalized)),
                               lhs,rhs)) {
        *arithmetic_shift=shift;ok=true;
    }
done:
    free(copy);return ok;
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

/* GAS .skip/.space/.zero share count[,fill] semantics.
 * Parse the optional fill once through the existing numeric grammar;
 * unsupported relocatable fills remain errors rather than fake zero bytes. */
/* Shared GAS .skip/.space/.zero expression grammar: evaluate absolute
 * arithmetic and already-resolved local differences via the existing
 * Common parser. Do not fabricate sizes for forward/relocatable symbols. */
static bool parse_common_space(MiniAs *as,const char *args,uint64_t *count,
                               uint8_t *fill) {
    char *copy=minias_strdup(args),*comma;
    int64_t amount,byte=0;
    bool ok;
    if(!copy)return false;
    comma=strchr(copy,',');
    if(comma){
        *comma++='\0';
        if(strchr(comma,',') ||
           !evaluate_absolute_expression(as,minias_trim(comma),&byte) ||
           byte<-128 || byte>255){free(copy);return false;}
    }
    ok=evaluate_absolute_expression(as,minias_trim(copy),&amount) &&
       amount>=0;
    free(copy);
    if(!ok)return false;
    *count=(uint64_t)amount;
    *fill=(uint8_t)byte;
    return true;
}

static bool emit_common_space(MiniAs *as,int section,size_t total,uint8_t fill) {
    unsigned char bytes[256];
    if(fill==0U)return minias_section_append_zero(as,section,total);
    memset(bytes,(int)fill,sizeof(bytes));
    while(total!=0U) {
        size_t chunk=total<sizeof(bytes)?total:sizeof(bytes);
        if(!minias_section_append(as,section,bytes,chunk))return false;
        total-=chunk;
    }
    return true;
}

/* Variable length GNU integer directives are DATA fragments, never
 * target opcodes. Parse only resolved absolute values; deferred symbolic
 * LEB widths require layout-aware fragments and must fail closed here. */
static bool minias_leb_bytes(MiniAs *as,const char *op,const char *text,
                             unsigned char output[10],size_t *length) {
    int64_t number;
    uint64_t bits;
    bool signed_leb=strcmp(op,".sleb128")==0;
    size_t n=0U;
    if(!evaluate_absolute_expression(as,text,&number) ||
       (!signed_leb && number<0))return false;
    bits=(uint64_t)number;
    for(;;) {
        uint8_t byte=(uint8_t)(bits&0x7fU);
        bool done;
        if(signed_leb) {
            number>>=7U;
            bits=(uint64_t)number;
            done=(number==0 && (byte&0x40U)==0U) ||
                 (number==-1 && (byte&0x40U)!=0U);
        }else {
            bits>>=7U;
            done=bits==0U;
        }
        if(n>=10U)return false;
        output[n++]=(unsigned char)(byte | (done?0U:0x80U));
        if(done){*length=n;return true;}
    }
}

/* Repeat a literal little-endian element: GNU .fill repeat[,size[,value]].
 * All targets use Common DATA fragments; no target-specific section writer. */
static bool parse_common_fill(MiniAs *as,const char *args,
                              uint64_t *repeat,unsigned *size,
                              uint64_t *bits) {
    char *copy=minias_strdup(args),*cursor,*parts[3]={NULL,NULL,NULL};
    unsigned i=0U;
    int64_t n,step=1,value=0;
    bool ok=false;
    if(!copy)return false;
    cursor=copy;
    for(;;) {
        char *comma=strchr(cursor,',');
        if(i>=3U)goto done;
        if(comma)*comma='\0';
        parts[i++]=minias_trim(cursor);
        if(!*parts[i-1U])goto done;
        if(!comma)break;
        cursor=comma+1U;
    }
    if(!evaluate_absolute_expression(as,parts[0],&n) || n<0)goto done;
    if(i>1U && (!evaluate_absolute_expression(as,parts[1],&step) ||
                step<0 || step>8))goto done;
    if(i>2U && !evaluate_absolute_expression(as,parts[2],&value))goto done;
    *repeat=(uint64_t)n;
    *size=(unsigned)step;
    *bits=(uint64_t)value;
    ok=true;
done:
    free(copy);return ok;
}

/* Size expressions with only numeric tokens are stable first-pass
 * fragments and must not disable the in-place relaxation fast path.
 * Symbolic names, location counter '.' and unresolved numeric syntax
 * enter the shared deferred solver. Scan tokens, not raw alphabetic
 * characters: 0xffULL is a number, not a forward symbol. */
static bool common_data_has_symbolic_token(const char *p) {
    while(*p) {
        if(isdigit((unsigned char)*p)) {
            char *end;
            uint64_t bits;
            if(minias_parse_number_literal(p,&end,&bits)) {
                (void)bits;
                p=end;
                continue;
            }
        }
        if(isalpha((unsigned char)*p) || *p=='_' || *p=='.' ||
           *p=='$')return true;
        ++p;
    }
    return false;
}

/* Recognize only real absolute .set symbols; never rewrite ordinary
 * relocatable labels or PC-relative expressions into guessed constants. */
static bool data_references_mutable_set(MiniAs *as,const char *expr) {
    const char *p=expr;
    while(*p) {
        if(*p=='.' || *p=='_' || *p=='$' ||
           isalpha((unsigned char)*p)) {
            const char *start=p;
            size_t len;
            char name[256];
            MiniAsSymbol *sym;
            do {++p;} while(isalnum((unsigned char)*p) ||
                            *p=='.' || *p=='_' || *p=='$');
            len=(size_t)(p-start);
            if(len>=sizeof(name))continue;
            memcpy(name,start,len);name[len]='\0';
            sym=minias_get_symbol(as,name,false);
            if(sym && sym->mutable_absolute_set)return true;
        }else ++p;
    }
    return false;
}

static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    unsigned int width = data_width(as, op);
    char *copy = NULL;
    char *cursor;
    char *snapshot=NULL;
    bool delayed=false;

    if (!strcmp(op,".fill")) {
        unsigned elem=0U;
        uint64_t repeats=0U,value=0U;
        delayed=common_data_has_symbolic_token(args);
        if(!parse_common_fill(as,args,&repeats,&elem,&value) ||
           (elem!=0U && repeats>UINT32_MAX/elem)) {
            delayed=true;
            /* Forward labels and subsection values have not received
             * their final layout yet. Keep this as a real data fragment
             * with provisional size 0, never as an ignored directive.
             * Invalid syntax/unresolved symbols are rejected at fixpoint. */
            bytes=0U;
        }else bytes=repeats*elem;
    } else if (strcmp(op,".uleb128")==0 || strcmp(op,".sleb128")==0) {
        unsigned char coded[10];
        size_t len=0U;
        if(!minias_leb_bytes(as,op,args,coded,&len)) {
            minias_set_error(as,"unsupported-leb-expression:%s:line=%zu",args,line);
            return false;
        }
        bytes=len;
    } else if (strcmp(op, ".incbin") == 0) {
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
    } else if (strcmp(op, ".zero") == 0 || strcmp(op, ".space") == 0 ||
               strcmp(op, ".skip") == 0) {
        uint8_t fill=0U;
        delayed=common_data_has_symbolic_token(args);
        if (!parse_common_space(as,args,&count,&fill)) {
            delayed=true;
            /* .skip/.space also permits forward absolute label differences.
             * A zero-length provisional fragment is resolved and verified
             * after all symbols have been placed. */
            count=0U;
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
            if (!minias_parse_absolute_data_bits(as, minias_trim(cursor), &value)) {
                MiniAsSymbolExpr expr;
                const char *trimmed = minias_trim(cursor);
                MiniAsSymbolExpr rhs_expr;
                uint64_t divisor;
                bool arithmetic_shift=false;
                bool supported =
                    ((width == 2U || width == 4U || width == 8U) &&
                     (parse_data_symbol_addend(trimmed, &expr) ||
                      parse_data_symbol_absolute_tail(as,trimmed,&expr))) ||
                    ((width == 2U || width == 4U || width == 8U) &&
                     (parse_symbol_minus_dot(trimmed, &expr) ||
                      parse_symbol_minus_dot_divisor(trimmed,
                                                     &expr,
                                                     &divisor))) ||
                    ((width == 1U || width == 2U || width == 4U || width == 8U) &&
                     (parse_symbol_difference(trimmed, &expr, &rhs_expr) ||
                      parse_symbol_difference_divisor(trimmed,&expr,
                              &rhs_expr,&divisor,&arithmetic_shift)));

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
    /*
     * GNU GAS freezes an absolute .set expression at its data emission
     * site, not at the last value after macro/.irp expansion. Store only
     * those parsed constants in the retained DATA statement string.
     * Leave all relocatable/forward references intact for layout/fixups.
     */
    if(width && as->has_mutable_absolute_sets && count) {
        size_t original_len=strlen(args),capacity;
        char *walk,*out;
        bool changed=false;
        size_t used=0U;
        if(count>(SIZE_MAX-original_len-1U)/24U) {
            minias_set_error(as,"set-data-snapshot-overflow");return false;
        }
        capacity=original_len+1U+(size_t)count*24U;
        snapshot=malloc(capacity);
        copy=minias_strdup(args);
        if(!snapshot || !copy) {
            free(snapshot);free(copy);
            minias_set_error(as,"out-of-memory:set-data-snapshot");return false;
        }
        walk=copy;out=snapshot;
        for(;;) {
            char *comma=strchr(walk,',');
            char *token;
            const char *selected;
            char numeric[32];
            size_t part;
            uint64_t value;
            if(comma)*comma='\0';
            token=minias_trim(walk);
            selected=token;
            if(data_references_mutable_set(as,token)) {
                MiniAsSymbolExpr reloc;
                if(minias_parse_absolute_data_bits(as,token,&value)) {
                    (void)snprintf(numeric,sizeof(numeric),"%llu",
                                   (unsigned long long)value);
                    selected=numeric;
                    changed=true;
                }else if(parse_data_symbol_absolute_tail(as,token,&reloc)) {
                    char *reloc_snapshot=malloc(strlen(reloc.name)+32U);
                    size_t needed;
                    if(!reloc_snapshot) {
                        free(copy);free(snapshot);
                        minias_set_error(as,"out-of-memory:set-reloc-snapshot");
                        return false;
                    }
                    needed=(size_t)snprintf(reloc_snapshot,
                                           strlen(reloc.name)+32U,
                                           "%s%+lld",reloc.name,
                                           (long long)reloc.addend);
                    if(needed>capacity-used-1U) {
                        free(reloc_snapshot);free(copy);free(snapshot);
                        minias_set_error(as,"set-reloc-snapshot-overflow");
                        return false;
                    }
                    memcpy(out+used,reloc_snapshot,needed);
                    used+=needed;
                    free(reloc_snapshot);
                    changed=true;
                    if(!comma)break;
                    out[used++]=',';
                    walk=comma+1U;
                    continue;
                }
            }
            part=strlen(selected);
            if(part>capacity-used-1U) {
                free(copy);free(snapshot);
                minias_set_error(as,"set-data-snapshot-overflow");return false;
            }
            memcpy(out+used,selected,part);used+=part;
            if(!comma)break;
            out[used++]=',';
            walk=comma+1U;
        }
        out[used]='\0';
        free(copy);copy=NULL;
        if(!changed){free(snapshot);snapshot=NULL;}
    }
    if (!add_stmt(as, MINIAS_STMT_DATA, op,
                  snapshot?snapshot:args, line, (uint32_t)bytes, 1U)) {
        free(snapshot);return false;
    }
    free(snapshot);
    if(delayed) {
        as->stmts[as->stmt_count-1U].deferred_data_size=true;
        as->has_deferred_data=true;
        as->layout_sensitive=true;
    }
    return advance_current_location(as, bytes);
}

static bool emit_symbol_minus_dot(MiniAs *as,
                                  const MiniAsStmt *stmt,
                                  unsigned int width,
                                  uint64_t relocation_offset,
                                  const MiniAsSymbolExpr *expr) {
    MiniAsSymbol *target = minias_get_symbol(as, expr->name, false);
    uint32_t add_type = as->target->fixup_relocation_type(MINIAS_FIXUP_ADD, width);
    uint32_t sub_type = as->target->fixup_relocation_type(MINIAS_FIXUP_SUB, width);

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

    /* ELF64 x86 has a direct signed PC32 relocation for S + A - P.
     * Unlike LoongArch ADD/SUB pairs, one semantic fixup is sufficient.
     * A target describes this choice, Common never tests its name. */
    {
        /* Both supported ELF64 targets directly represent S+A-P at
         * either 32 or 64 bits. The Target reports whether it supports
         * the semantic relocation; no architecture-name branching here. */
        MiniAsFixupKind kind=width==4U?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if((width==4U || width==8U) &&
           kind!=MINIAS_FIXUP_NONE &&
           as->target->fixup_relocation_type(kind,width)!=0U)
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup(as,stmt->section,relocation_offset,
                                    kind,width,expr->name,expr->addend);
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
               minias_add_fixup(as,
                                     stmt->section,
                                     relocation_offset,
                                     MINIAS_FIXUP_ADD,
                                     width,
                                     expr->name,
                                     expr->addend) &&
               minias_add_fixup(as,
                                     stmt->section,
                                     relocation_offset,
                                     MINIAS_FIXUP_SUB,
                                     width,
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
    uint32_t add_type = as->target->fixup_relocation_type(MINIAS_FIXUP_ADD, width);
    uint32_t sub_type = as->target->fixup_relocation_type(MINIAS_FIXUP_SUB, width);

    if (lhs != NULL && rhs != NULL && lhs->defined && rhs->defined &&
        /* If both names resolve to the same real section, its load
         * address cancels. GNU GAS resolves GLOBAL, WEAK and local names
         * alike, including when the data value belongs to a different
         * output section. Never fold SHN_COMMON/UNDEF or cross-section
         * operands: they are not absolute without linker semantics.
         * Keep target ADD/SUB pairs on RISC-V/LoongArch, where relocation
         * contracts are required by kernel .alternative patching. */
        lhs->section == rhs->section &&
        (lhs->section >= 0 || lhs->section == MINIAS_SECTION_ABS) &&
        (add_type == 0U || sub_type == 0U)) {
        /* Modular arithmetic gives correct low 8/16/32/64 bits and avoids
         * undefined signed overflow for large absolute/forward symbols. */
        uint64_t value=(lhs->value+(uint64_t)lhs_expr->addend)-
                       (rhs->value+(uint64_t)rhs_expr->addend);
        unsigned char bytes[8];
        unsigned int i;

        for (i = 0U; i < width; ++i) {
            bytes[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
        }
        return minias_section_append(as, stmt->section, bytes, width);
    }

    /*
     * GNU GAS ELF64 canonicalizes S1-S2 when S2 is a local anchor in
     * the same section as the relocation field:
     *   S1 - S2 + A = S1 - P + (P - S2 + A)
     * The right side is a single ELF PC32/PC64 fixup, even if S1 is
     * external or belongs to a different section. This avoids inventing
     * unsupported ADD/SUB relocation pairs on x86/AArch64. Targets with
     * genuine paired relocations continue to use their existing path.
     */
    if((width==4U || width==8U) &&
       (add_type==0U || sub_type==0U) &&
       rhs!=NULL && rhs->defined &&
       /* GNU GAS resolves defined GLOBAL/WEAK anchors to their section
        * positions for PC64 as well as LOCAL anchors. This binding
        * behavior is the same on x86-64 and AArch64. */
       rhs->section==stmt->section) {
        MiniAsFixupKind kind=width==4U?
            as->target->dot_relative_fixup:MINIAS_FIXUP_PREL64;
        if(kind!=MINIAS_FIXUP_NONE &&
           as->target->fixup_relocation_type(kind,width)!=0U) {
            uint64_t delta=relocation_offset-rhs->value+
                           (uint64_t)lhs_expr->addend-
                           (uint64_t)rhs_expr->addend;
            return minias_section_append_zero(as,stmt->section,width) &&
                   minias_add_fixup(as,stmt->section,relocation_offset,
                                    kind,width,lhs_expr->name,(int64_t)delta);
        }
    }
    if (add_type == 0U || sub_type == 0U) {
        minias_set_error(as,
                         "unsupported-symbol-difference-width:%u:line=%zu",
                         width,
                         stmt->line);
        return false;
    }
    return minias_section_append_zero(as, stmt->section, width) &&
           minias_add_fixup(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_ADD,
                                 width,
                                 lhs_expr->name,
                                 lhs_expr->addend) &&
           minias_add_fixup(as,
                                 stmt->section,
                                 relocation_offset,
                                 MINIAS_FIXUP_SUB,
                                 width,
                                 rhs_expr->name,
                                 rhs_expr->addend);
}

static bool emit_symbol_difference_divided(MiniAs *as,
        const MiniAsStmt *stmt,unsigned int width,
        const MiniAsSymbolExpr *lhs_expr,
        const MiniAsSymbolExpr *rhs_expr,uint64_t divisor,
        bool arithmetic_shift) {
    MiniAsSymbol *lhs=minias_get_symbol(as,lhs_expr->name,false);
    MiniAsSymbol *rhs=minias_get_symbol(as,rhs_expr->name,false);
    int64_t left,right,diff,quotient;
    unsigned char bytes[8];
    unsigned i;
    /* A cross-section difference cannot be divided using an ELF fixup.
     * Keep the operation fail-closed rather than fabricating a value. */
    if(!lhs || !rhs || !lhs->defined || !rhs->defined ||
       lhs->section!=rhs->section ||
       (lhs->section<0 && lhs->section!=MINIAS_SECTION_ABS) ||
       lhs->value>(uint64_t)INT64_MAX ||
       rhs->value>(uint64_t)INT64_MAX ||
       divisor==0U || divisor>(uint64_t)INT64_MAX ||
       !checked_add_i64((int64_t)lhs->value,lhs_expr->addend,&left) ||
       !checked_add_i64((int64_t)rhs->value,rhs_expr->addend,&right) ||
       !checked_sub_i64(left,right,&diff)) {
        minias_set_error(as,"unsupported-divided-symbol-pair:line=%zu",
                         stmt->line);
        return false;
    }
    if(arithmetic_shift) {
        uint64_t shifted=(uint64_t)diff/divisor;
        if(width<8U &&
           shifted>((UINT64_C(1)<<(width*8U))-1U)) {
            minias_set_error(as,
                             "shifted-symbol-pair-field-overflow:line=%zu",
                             stmt->line);
            return false;
        }
        quotient=(int64_t)shifted;
    }else quotient=diff/(int64_t)divisor;
    for(i=0U;i<width;++i)
        bytes[i]=(unsigned char)((uint64_t)quotient>>(8U*i));
    return minias_section_append(as,stmt->section,bytes,width);
}

static bool emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    unsigned int width = data_width(as, stmt->op);
    char *copy;
    char *cursor;

    if(!strcmp(stmt->op,".fill")) {
        uint64_t repeat,value,i,j;
        unsigned width_bytes;
        unsigned char element[8];
        as->expr_dot_override=true;
        as->expr_dot_value=stmt->offset;
        {
            bool valid=parse_common_fill(as,stmt->args,
                                         &repeat,&width_bytes,&value);
            as->expr_dot_override=false;
            if(!valid || (width_bytes!=0U && repeat>UINT32_MAX/width_bytes) ||
               repeat*width_bytes!=stmt->size) {
                minias_set_error(as,"fill-layout-changed:line=%zu",stmt->line);
                return false;
            }
        }
        for(j=0U;j<width_bytes;++j)
            element[j]=(unsigned char)(value>>(8U*j));
        for(i=0U;i<repeat;++i)
            if(!minias_section_append(as,stmt->section,element,width_bytes))
                return false;
        return true;
    }
    if(strcmp(stmt->op,".uleb128")==0 ||
       strcmp(stmt->op,".sleb128")==0) {
        unsigned char coded[10];
        size_t len=0U;
        if(!minias_leb_bytes(as,stmt->op,stmt->args,coded,&len) ||
           len!=stmt->size) {
            minias_set_error(as,"leb-layout-changed:line=%zu",stmt->line);
            return false;
        }
        return minias_section_append(as,stmt->section,coded,len);
    }
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
    if (strcmp(stmt->op, ".zero") == 0 || strcmp(stmt->op, ".space") == 0 ||
        strcmp(stmt->op, ".skip") == 0) {
        uint64_t count;
        uint8_t fill;
        bool valid;
        as->expr_dot_override=true;
        as->expr_dot_value=stmt->offset;
        valid=parse_common_space(as,stmt->args,&count,&fill);
        as->expr_dot_override=false;
        if(!valid || count!=stmt->size) {
            minias_set_error(as,"invalid-space-emit:line=%zu",stmt->line);
            return false;
        }
        return emit_common_space(as,stmt->section,stmt->size,fill);
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
        if (!minias_parse_absolute_data_bits(as, minias_trim(cursor), &value)) {
            MiniAsSymbolExpr expr;
            const char *trimmed = minias_trim(cursor);
            uint64_t relocation_offset =
                (uint64_t)as->sections[(size_t)stmt->section].size;

            {
                MiniAsSymbolExpr rhs_expr;
                uint64_t divisor;
                bool arithmetic_shift=false;
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
                } else if ((width == 1U || width == 2U || width == 4U ||
                            width == 8U) &&
                           parse_symbol_difference_divisor(trimmed,&expr,
                                  &rhs_expr,&divisor,&arithmetic_shift)) {
                    if(!emit_symbol_difference_divided(as,stmt,width,
                              &expr,&rhs_expr,divisor,arithmetic_shift)) {
                        free(copy);return false;
                    }
                } else if ((width == 1U || width == 2U || width == 4U ||
                            width == 8U) &&
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
                    if ((width != 4U && width != 8U) ||
                        !(parse_data_symbol_addend(trimmed,&expr) ||
                          parse_data_symbol_absolute_tail(as,trimmed,&expr))) {
                        minias_set_error(as,
                                         "unsupported-expression:%s:%s:line=%zu",
                                         stmt->op,
                                         trimmed,
                                         stmt->line);
                        free(copy);
                        return false;
                    }
                    if (!minias_section_append_zero(as, stmt->section, width) ||
                        !minias_add_fixup(as,
                                              stmt->section,
                                              relocation_offset,
                                              MINIAS_FIXUP_ABS,
                                              width,
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
            (p == text || numeric_ref_boundary(p[-1]) ||
             /* Immediate introducer $ precedes GNU local labels $1f/$2b,
              * but must not split the suffix of a longer symbol foo$1f. */
             (p[-1] == '$' &&
              ((size_t)(p - text) == 1U || numeric_ref_boundary(p[-2]))))) {
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
    symbol->has_fragment_anchor = true;
    symbol->fragment_anchor = as->stmt_count;
    if (!current_location(as, &symbol->value)) {
        symbol->defined = false;
        return false;
    }
    return true;
}

/* Materialize real GNU literal pools at the requested location, NOT by
 * ignoring .ltorg: each retained request gets an aligned DATA fragment,
 * a fragment-anchored local label and a normal ABS32/ABS64 relocation.
 * Zero-fill inside executable sections matches GNU GAS, not NOP padding. */
static bool flush_literal_pools(MiniAs *as,size_t line,bool all) {
    int original_section=as->current_section;
    uint32_t original_subsection=as->current_subsection;
    size_t i;
    unsigned width;
    /* GNU GAS groups the pending A64 literal pool by DATA WIDTH before
     * first-use order: 32-bit entries precede 64-bit entries.  FIFO
     * insertion alone shifts the PC-relative LDR imm19 and relocations
     * for mixed W/X =symbol requests; source byte oracle proves this. */
    for(width=4U;width<=8U;width*=2U) {
    for(i=0U;i<as->literal_count;++i) {
        MiniAsLiteral *entry=&as->literals[i];
        if(entry->width!=width)continue;
        char alignment[16],value[384],label[64];
        int written;
        if(entry->emitted ||
           (!all && (entry->section!=as->current_section ||
                     entry->subsection!=as->current_subsection)))continue;
        if(all) {
            as->current_section=entry->section;
            as->current_subsection=entry->subsection;
            if(!ensure_subsection(as,as->current_section,as->current_subsection))
                goto failure;
        }
        written=snprintf(alignment,sizeof(alignment),"%u,0",entry->width);
        if(written<0 || (size_t)written>=sizeof(alignment))goto failure;
        if(!handle_align(as,".balign",alignment,line))goto failure;
        written=snprintf(label,sizeof(label),"%s",entry->label);
        if(written<0 || (size_t)written>=sizeof(label))goto failure;
        if(!define_label(as,label,line))goto failure;
        written=snprintf(value,sizeof(value),"%s%+lld",
                         entry->expression.name,
                         (long long)entry->expression.addend);
        if(written<0 || (size_t)written>=sizeof(value))goto failure;
        if(!add_data_stmt(as,entry->width==8U?".quad":".long",value,line))
            goto failure;
        entry->emitted=true;
    }
    }
    as->current_section=original_section;
    as->current_subsection=original_subsection;
    return true;
failure:
    as->current_section=original_section;
    as->current_subsection=original_subsection;
    return false;
}

/* Tentative COMMON definitions have ELF st_value=alignment, st_size=length.
 * Explicit .local symbols instead acquire real .bss space through the
 * existing Common fragment/section layout and symbol-anchor pipeline. */
static bool parse_comm(MiniAs *as, const char *args, size_t line) {
    char *copy=minias_strdup(args);
    char *name,*size_text,*align_text=NULL,*first,*second;
    MiniAsSymbol *symbol;
    uint64_t alignment=1U;
    int64_t size,alignment_value;
    size_t k;
    if(!copy)return false;
    first=strchr(copy,',');
    if(!first)goto invalid;
    *first++='\0';
    second=strchr(first,',');
    if(second) {
        *second++='\0';
        if(strchr(second,','))goto invalid;
        align_text=minias_trim(second);
        if(!*align_text)goto invalid;
    }
    name=minias_trim(copy);size_text=minias_trim(first);
    if(!absolute_symbol_start(*name) || !*size_text)goto invalid;
    for(k=1U;name[k];++k)
        if(!absolute_symbol_continue(name[k]))goto invalid;
    if(!evaluate_absolute_expression(as,size_text,&size) || size<0)
        goto invalid;
    if(align_text) {
        if(!evaluate_absolute_expression(as,align_text,&alignment_value) ||
           alignment_value<=0)goto invalid;
        alignment=(uint64_t)alignment_value;
    } else {
        /* GNU x86 uses min(nextpow2(size),16). This default is a target
         * descriptor; unverified target dialects must fail closed. */
        unsigned cap=as->target->common_default_alignment_cap;
        if(cap==0U || (cap&(cap-1U))!=0U)goto invalid;
        while(alignment<(uint64_t)size && alignment<(uint64_t)cap)
            alignment*=2U;
    }
    if((alignment&(alignment-1U))!=0U)goto invalid;
    symbol=minias_get_symbol(as,name,true);
    if(!symbol){free(copy);return false;}
    if(symbol->alias_pending || symbol->type==MINIAS_STT_FUNC ||
       symbol->bind==MINIAS_STB_WEAK)goto invalid;
    if(symbol->defined) {
        /* GNU GAS retains first tentative size, maxes alignment. */
        if(symbol->section!=MINIAS_SECTION_COMMON)goto invalid;
        if(alignment>symbol->value)symbol->value=alignment;
        free(copy);return true;
    }
    if(symbol->explicit_local_binding) {
        int saved_section=as->current_section;
        uint32_t saved_subsection=as->current_subsection;
        int bss;
        char align_buf[32],size_buf[32];
        bool ok;
        if((uint64_t)size>UINT32_MAX)goto invalid;
        bss=minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U);
        if(bss<0){free(copy);return false;}
        as->current_section=bss;as->current_subsection=0U;
        (void)snprintf(align_buf,sizeof(align_buf),"%llu",
                       (unsigned long long)alignment);
        (void)snprintf(size_buf,sizeof(size_buf),"%llu",
                       (unsigned long long)size);
        ok=ensure_subsection(as,bss,0U)!=NULL &&
           handle_align(as,".balign",align_buf,line) &&
           define_label(as,name,line) &&
           add_data_stmt(as,".zero",size_buf,line);
        as->current_section=saved_section;
        as->current_subsection=saved_subsection;
        if(!ok){free(copy);return false;}
        symbol=minias_get_symbol(as,name,false);
        if(!symbol){free(copy);return false;}
        symbol->type=MINIAS_STT_OBJECT;
        symbol->size=(uint64_t)size;
        free(copy);return true;
    }
    symbol->defined=true;
    symbol->bind=MINIAS_STB_GLOBAL;
    symbol->type=MINIAS_STT_OBJECT;
    symbol->section=MINIAS_SECTION_COMMON;
    symbol->subsection=0U;
    symbol->size=(uint64_t)size;
    symbol->value=alignment;
    free(copy);return true;
invalid:
    minias_set_error(as,"unsupported-common:%s:line=%zu",args,line);
    free(copy);return false;
}

/* GNU .reloc uses a section-relative OFFSET and symbolic relocation
 * name; no object bytes are emitted by this directive. The Target owns
 * the relocation name->semantic fixup mapping, and the existing Common
 * Fixup/ELF path serializes the entry. Never invent a numeric ELF type. */
static bool handle_named_reloc(MiniAs *as,const char *args,size_t line) {
    char *copy=minias_strdup(args),*comma1,*comma2,*offset_text,*name,*symbol;
    MiniAsFixupKind kind=MINIAS_FIXUP_NONE;
    MiniAsSymbolExpr expression;
    unsigned width=0U;
    int64_t offset;
    bool ok;
    if(!copy){minias_set_error(as,"out-of-memory:.reloc");return false;}
    comma1=strchr(copy,',');
    if(!comma1){free(copy);goto invalid;}
    *comma1++='\0';
    comma2=strchr(comma1,',');
    if(!comma2){free(copy);goto invalid;}
    *comma2++='\0';
    offset_text=minias_trim(copy);
    name=minias_trim(comma1);
    symbol=minias_trim(comma2);
    ok=as->current_section>=0 && as->current_subsection==0U &&
       as->target->parse_named_relocation &&
       as->target->parse_named_relocation(name,&kind,&width) &&
       *symbol!='\0' && !strchr(symbol,',') &&
       evaluate_absolute_expression(as,offset_text,&offset) && offset>=0 &&
       minias_parse_symbol_addend(symbol,&expression);
    if(ok)ok=minias_add_fixup(as,as->current_section,(uint64_t)offset,
                              kind,width,expression.name,expression.addend);
    free(copy);
    if(ok)return true;
invalid:
    minias_set_error(as,"bad-directive:.reloc:%s:line=%zu",args,line);
    return false;
}

/* Statement ordinal is stable across re-layout; macro source lines are not. */
static bool force_long_branch_for_stmt(const MiniAs *as, size_t statement_id) {
    size_t i;

    for (i = 0U; i < as->forced_long_branch_count; ++i) {
        if (as->forced_long_branch_ids[i] == statement_id) {
            return true;
        }
    }
    return false;
}

/* Layout iteration owns fragment identity and growth bookkeeping. Only
 * target backends know the instruction's displacement range and expansion. */

/* Persistent fragment layout: parsed statements are the fragments. Re-layout
 * only their offsets and sizes, preserve expanded macro identities, and
 * relocate label/.size anchors without reopening or reparsing the source.
 *
 * Sources containing position-dependent conditionals, .org, .set or .equ
 * retain the conservative reparsing path until these directives acquire
 * typed deferred-expression fragments. */
static bool minias_relayout_fragments(MiniAs *as) {
    size_t n = as->stmt_count, nsym = as->symbol_count;
    size_t *start_head = NULL, *end_head = NULL;
    size_t *start_next = NULL, *end_next = NULL;
    size_t i;
    bool ok = false;

    if (n == SIZE_MAX || n + 1U > SIZE_MAX / sizeof(size_t) ||
        (nsym != 0U && nsym > SIZE_MAX / sizeof(size_t))) {
        minias_set_error(as, "fragment-anchor-overflow");
        return false;
    }
    start_head = malloc((n + 1U) * sizeof(size_t));
    end_head = malloc((n + 1U) * sizeof(size_t));
    start_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    end_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    if (start_head == NULL || end_head == NULL ||
        start_next == NULL || end_next == NULL) {
        minias_set_error(as, "out-of-memory:fragment-anchors");
        goto done;
    }
    for (i = 0U; i <= n; ++i) {
        start_head[i] = SIZE_MAX;
        end_head[i] = SIZE_MAX;
    }
    for (i = 0U; i < as->subsection_count; ++i)
        as->subsections[i].size = 0U;

    for (i = 0U; i < nsym; ++i) {
        MiniAsSymbol *sym = &as->symbols[i];
        if (sym->has_fragment_anchor && sym->defined && sym->section >= 0) {
            size_t anchor = sym->fragment_anchor;
            if (anchor > n) {
                minias_set_error(as, "internal:fragment-symbol-anchor:%s",
                                 sym->name);
                goto done;
            }
            start_next[i] = start_head[anchor];
            start_head[anchor] = i;
        }
        if (sym->has_size_end_anchor) {
            size_t anchor = sym->size_end_anchor;
            if (anchor > n) {
                minias_set_error(as, "internal:fragment-size-anchor:%s",
                                 sym->name);
                goto done;
            }
            end_next[i] = end_head[anchor];
            end_head[anchor] = i;
        }
    }

    for (i = 0U; i <= n; ++i) {
        size_t index;
        for (index = start_head[i]; index != SIZE_MAX;
             index = start_next[index]) {
            MiniAsSymbol *sym = &as->symbols[index];
            MiniAsSubsection *sub =
                find_subsection(as, sym->section, sym->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-symbol-subsection:%s",
                                 sym->name);
                goto done;
            }
            sym->value = sub->size;
        }
        for (index = end_head[i]; index != SIZE_MAX;
             index = end_next[index]) {
            MiniAsSymbol *sym = &as->symbols[index];
            MiniAsSubsection *sub =
                find_subsection(as, sym->section, sym->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-size-subsection:%s",
                                 sym->name);
                goto done;
            }
            sym->size_end_value = sub->size;
        }
        if (i < n) {
            MiniAsStmt *stmt = &as->stmts[i];
            MiniAsSubsection *sub =
                find_subsection(as, stmt->section, stmt->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-subsection");
                goto done;
            }
            stmt->offset = sub->size;
            if (stmt->kind == MINIAS_STMT_ALIGN &&
                strcmp(stmt->op, ".org") != 0) {
                uint64_t align = stmt->align;
                uint64_t pad;
                if (align == 0U || (align & (align - 1U)) != 0U) {
                    minias_set_error(as, "bad-fragment-alignment");
                    goto done;
                }
                pad = (align - (sub->size & (align - 1U))) & (align - 1U);
                if (stmt->align_has_max_skip && pad > stmt->align_max_skip)
                    pad = 0U;
                if (pad > UINT32_MAX) {
                    minias_set_error(as, "fragment-alignment-overflow");
                    goto done;
                }
                stmt->size = (uint32_t)pad;
            }
            if ((uint64_t)stmt->size > UINT64_MAX - sub->size) {
                minias_set_error(as, "fragment-size-overflow");
                goto done;
            }
            sub->size += stmt->size;
        }
    }

    for (i = 0U; i < nsym; ++i) {
        MiniAsSymbol *sym = &as->symbols[i];
        if (sym->has_size_end_anchor) {
            if (!sym->has_fragment_anchor ||
                sym->size_end_value < sym->value) {
                minias_set_error(as, "fragment-size-location-mismatch:%s",
                                 sym->name);
                goto done;
            }
            sym->size = sym->size_end_value - sym->value;
        }
    }
    ok = finalize_subsection_layout(as);
done:
    free(start_head);
    free(end_head);
    free(start_next);
    free(end_next);
    return ok;
}

/* Resolve Common variable-size fragments with the exact post-subsection
 * symbol addresses.  Re-run shared Fragment/Layout ownership until the
 * sizes stop changing.  This handles GNU forward numeric labels and
 * expressions referencing later subsections without backend-specific
 * preprocessor passes, speculative dummy symbols, or unknown-opcode skips.
 *
 * The parser already has every source symbol at this point.  A reference
 * to a truly undefined label must remain an error, never a zero fill. */
static bool minias_resolve_deferred_data(MiniAs *as) {
    size_t pass;
    int previous_section=as->current_section;
    uint32_t previous_subsection=as->current_subsection;
    if(!as->has_deferred_data)return true;
    /* The original first-pass layout is also provisional: .fill may have
     * been measured before a forward symbol's section was known. */
    if(!minias_relayout_fragments(as))return false;
    for(pass=0U;pass<=as->stmt_count;++pass) {
        size_t i;
        bool changed=false;
        for(i=0U;i<as->stmt_count;++i) {
            MiniAsStmt *stmt=&as->stmts[i];
            uint64_t n=0U;
            if(!stmt->deferred_data_size)continue;
            as->current_section=stmt->section;
            as->current_subsection=stmt->subsection;
            as->expr_dot_override=true;
            as->expr_dot_value=stmt->offset;
            if(!strcmp(stmt->op,".fill")) {
                unsigned width=0U;
                uint64_t repeat=0U,bits=0U;
                if(!parse_common_fill(as,stmt->args,&repeat,&width,&bits) ||
                   (width!=0U && repeat>UINT32_MAX/width)) {
                    minias_set_error(as,
                        "unresolved-deferred-fill:%s:line=%zu",
                        stmt->args,stmt->line);
                    goto invalid;
                }
                n=repeat*width;
            }else {
                uint64_t amount=0U;
                uint8_t fill=0U;
                if(!parse_common_space(as,stmt->args,&amount,&fill) ||
                   amount>UINT32_MAX) {
                    minias_set_error(as,
                        "unresolved-deferred-space:%s:line=%zu",
                        stmt->args,stmt->line);
                    goto invalid;
                }
                n=amount;
            }
            as->expr_dot_override=false;
            if(n!=(uint64_t)stmt->size) {
                stmt->size=(uint32_t)n;
                changed=true;
            }
        }
        if(!changed) {
            as->current_section=previous_section;
            as->current_subsection=previous_subsection;
            return true;
        }
        if(!minias_relayout_fragments(as))goto invalid;
    }
    minias_set_error(as,"deferred-fragment-layout-nonconvergent");
invalid:
    as->expr_dot_override=false;
    as->current_section=previous_section;
    as->current_subsection=previous_subsection;
    return false;
}

static bool collect_out_of_range_branches(MiniAs *as,
                                          size_t **ids,
                                          size_t *count,
                                          size_t *capacity,
                                          bool *added) {
    size_t i;
    *added = false;
    if (as->target->relax_instruction == NULL) return true;
    for (i = 0U; i < as->stmt_count; ++i) {
        const MiniAsStmt *stmt = &as->stmts[i];
        uint32_t expanded = stmt->size;
        size_t j;
        bool known = false;
        if (stmt->kind != MINIAS_STMT_INSN ||
            !as->target->relax_instruction(as, stmt, &expanded)) {
            if (as->error[0] != '\0') return false;
            continue;
        }
        if (expanded <= stmt->size) continue;
        if (as->target->expanded_instruction_size == NULL) {
            minias_set_error(as, "target-relaxation-size-unavailable:%s",
                             stmt->op);
            return false;
        }
        for (j = 0U; j < *count; ++j) {
            if ((*ids)[j] == i) { known = true; break; }
        }
        if (known) continue;
        if (!grow_array((void **)ids, capacity, sizeof(**ids), *count + 1U)) {
            minias_set_error(as, "out-of-memory:branch-relaxation");
            return false;
        }
        (*ids)[(*count)++] = i;
        *added = true;
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
    bool had_leading_label=false;

    text = minias_trim(text);
    if (*text == '\0') {
        return true;
    }

    /* GNU's preprocessed native .S contains CPP source line markers.
     * This form is common to all architectures; AArch64 also uses '#'
     * in real instruction immediates, so only strict # N "file" is metadata. */
    /* GNU GCC/Clang inline-assembler boundaries are exact standalone
     * source markers. AArch64 uses # for immediate operands, so do not
     * treat arbitrary #foo or #number as a comment. */
    if (strcmp(text, "#APP") == 0 || strcmp(text, "#NO_APP") == 0 ||
        strcmp(text, "#NOAPP") == 0) return true;
    /* At column zero after trim, # starts a GNU assembler comment;
     * this does NOT suppress #immediate in A64 instruction operands. */
    if(text[0]=='#' && isspace((unsigned char)text[1]))return true;
    if (text[0] == '#' && isspace((unsigned char)text[1])) {
        const char *marker = text + 2;
        while (isspace((unsigned char)*marker)) ++marker;
        if (isdigit((unsigned char)*marker)) {
            while (isdigit((unsigned char)*marker)) ++marker;
            if (isspace((unsigned char)*marker)) {
                while (isspace((unsigned char)*marker)) ++marker;
                if (*marker == '"' && strchr(marker + 1, '"') != NULL)
                    return true;
            }
        }
    }

    /* GNU .ifb/.ifnb compare the *expanded* argument against blank,
     * without any target ISA grammar; shared conditional stack owns them. */
    if((strncmp(text,".ifb",4U)==0 ||
        strncmp(text,".ifnb",5U)==0) &&
       ((text[3]=='b' && (text[4]=='\0' ||
                         isspace((unsigned char)text[4]))) ||
        (text[3]=='n' && text[4]=='b' &&
         (text[5]=='\0' || isspace((unsigned char)text[5]))))) {
        bool nonblank=text[3]=='n';
        const char *arg=text+(nonblank?5U:4U);
        while(isspace((unsigned char)*arg))++arg;
        as->layout_sensitive=true;
        return push_conditional_value(as,nonblank?*arg!='\0':*arg=='\0');
    }
    if ((strncmp(text,".ifeqs",6U)==0 ||
         strncmp(text,".ifnes",6U)==0) &&
        (text[6]=='\0' || isspace((unsigned char)text[6]))) {
        bool unequal=text[3]=='n';
        as->layout_sensitive=true;
        return push_conditional_strings(as,minias_trim(text+6U),
                                        unequal,true,line);
    }
    if ((strncmp(text,".ifc",4U)==0 ||
         strncmp(text,".ifnc",5U)==0) &&
        ((text[3]=='c' && (text[4]=='\0' ||
                          isspace((unsigned char)text[4]))) ||
         (text[3]=='n' && text[4]=='c' &&
          (text[5]=='\0' || isspace((unsigned char)text[5]))))) {
        bool unequal=text[3]=='n';
        as->layout_sensitive=true;
        return push_conditional_strings(as,
                 minias_trim(text+(unequal?5:4)),unequal,false,line);
    }
    /* GNU .ifnotdef and .ifndef are aliases. */
    if ((strncmp(text,".ifdef",6U)==0 &&
         (text[6]=='\0' || isspace((unsigned char)text[6]))) ||
        (strncmp(text,".ifndef",7U)==0 &&
         (text[7]=='\0' || isspace((unsigned char)text[7]))) ||
        (strncmp(text,".ifnotdef",9U)==0 &&
         (text[9]=='\0' || isspace((unsigned char)text[9])))) {
        bool negate=text[3]=='n';
        unsigned width=negate?(text[4]=='o'?9U:7U):6U;
        as->layout_sensitive=true;
        return push_conditional_defined(as,minias_trim(text+width),
                                        negate,line);
    }
    {
        static const char *const numeric_forms[]={
            ".ifeq",".ifne",".iflt",".ifle",".ifgt",".ifge"
        };
        unsigned i;
        for(i=0U;i<sizeof(numeric_forms)/sizeof(numeric_forms[0]);++i) {
            const char *name=numeric_forms[i];
            size_t width=strlen(name);
            if(strncmp(text,name,width)==0 &&
               (text[width]=='\0' || isspace((unsigned char)text[width]))) {
                as->layout_sensitive=true;
                return push_conditional_numeric(as,
                           minias_trim(text+width),name,line);
            }
        }
    }
    if (strncmp(text, ".if", 3U) == 0 &&
        (text[3] == '\0' || isspace((unsigned char)text[3]))) {
        /* .if may inspect '.', local symbol values and derived absolute
         * symbols; only reparsing fully reproduces that control flow. */
        as->layout_sensitive = true;
        return push_conditional(as, minias_trim(text + 3), line);
    }
    if (strncmp(text, ".elseif", 7U) == 0 &&
        (text[7] == '\0' || isspace((unsigned char)text[7]))) {
        as->layout_sensitive=true;
        return elseif_conditional(as,minias_trim(text+7U),line);
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
        had_leading_label=true;
        if (!define_label(as, text, line)) {
            return false;
        }
        text = minias_trim(colon + 1);
        if (*text == '\0') {
            return true;
        }
    }
    /* A GNU statement after LABEL: still starts a fresh directive.
     * Reenter the same shared conditional dispatcher instead of treating
     * '0: .ifc \arg,x' as an unknown target instruction/directive. */
    if(had_leading_label)return process_statement(as,text,line);

    {
        char *glued=NULL;
        /* GNU's .inst(EXPR) and .long(EXPR) syntax permits interior
         * whitespace and nested C-preprocessed arithmetic. It must be
         * recognized BEFORE normal token splitting, which would otherwise
         * treat the first space inside EXPR as the end of the directive.
         * The width hook is shared and TargetInfo-owned: no A64-specific
         * lexer path and no accepting arbitrary unknown .directives. */
        if(text[0]=='.') {
            char *open=strchr(text,'(');
            char *space_first=strpbrk(text," \t");
            if(open && (!space_first || open<space_first)) {
                char *end=text+strlen(text);
                while(end>open+1U && isspace((unsigned char)end[-1]))--end;
                if(end>open+1U && end[-1]==')') {
                    *open='\0';
                    if(data_width(as,text)!=0U) {
                        end[-1]='\0';
                        glued=minias_trim(open+1U);
                    }else *open='(';
                }
            }
        }
        if(glued)args=glued;
        else {
            space=text;
            while(*space!='\0' && !isspace((unsigned char)*space))
                ++space;
            if(*space) {
                *space='\0';
                args=minias_trim(space+1U);
            }else args=space;
        }
    }
    op = text;

    /* GNU GAS also accepts assignments without surrounding spaces:
     * vector=0x20 and vector=vector+1 inside .rept. Both spellings must
     * reach the SAME mutable Common .set owner, not Target ISA parsing.
     * Keep `.L2=.` anchored at the current fragment boundary. */
    if ((args[0] == '=' && args[1] != '=') ||
        (strchr(op, '=') != NULL && args[0] == '\0')) {
        char *attached = strchr(op, '=');
        char *rhs;
        char *set_args;
        if (attached != NULL) {
            *attached++ = '\0';
            rhs = minias_trim(attached);
        } else {
            rhs = minias_trim(args + 1U);
        }
        size_t name_len = strlen(op), rhs_len = strlen(rhs), k;
        bool ok;
        if (!rhs_len || !name_len ||
            !(isalpha((unsigned char)op[0]) ||
              op[0] == '.' || op[0] == '_' || op[0] == '$')) {
            minias_set_error(as,"bad-gnu-assignment:line=%zu",line);
            return false;
        }
        for (k=1U;k<name_len;++k) {
            if (!(isalnum((unsigned char)op[k]) ||
                  op[k] == '.' || op[k] == '_' || op[k] == '$')) {
                minias_set_error(as,"bad-gnu-assignment-symbol:line=%zu",line);
                return false;
            }
        }
        if (!strcmp(rhs,".")) return define_label(as,op,line);
        if (name_len > SIZE_MAX-rhs_len-2U) {
            minias_set_error(as,"gnu-assignment-too-long:line=%zu",line);
            return false;
        }
        set_args=malloc(name_len+rhs_len+2U);
        if (!set_args) {
            minias_set_error(as,"out-of-memory:gnu-assignment");
            return false;
        }
        memcpy(set_args,op,name_len);
        set_args[name_len]=',';
        memcpy(set_args+name_len+1U,rhs,rhs_len+1U);
        as->layout_sensitive=true;
        ok=parse_set(as,set_args,line);
        free(set_args);
        return ok;
    }

    /*
     * Linux arch/x86/include/asm/asm.h declares extable_type_reg with
     * .macro and releases it via .purgem. Without the purge, the next
     * definition is rejected or stale parameters leak into a new expansion.
     * GNU permits purging a non-existing name (with a warning), so keep it
     * idempotent. Don't invalidate an active expansion's borrowed body.
     */
    if(!strcmp(op,".purgem")) {
        char *name=minias_trim(args);
        size_t i;
        if(!*name || strpbrk(name,", \t\r")!=NULL) {
            minias_set_error(as,"bad-directive:.purgem:%s:line=%zu",name,line);
            return false;
        }
        as->layout_sensitive=true;
        for(i=0U;i<as->macro_count;++i)
            if(!strcmp(as->macros[i].name,name)) {
                release_macro(&as->macros[i]);
                if(i+1U<as->macro_count)
                    memmove(&as->macros[i],&as->macros[i+1U],
                            (as->macro_count-i-1U)*sizeof(*as->macros));
                --as->macro_count;
                memset(&as->macros[as->macro_count],0,
                       sizeof(*as->macros));
                break;
            }
        return true;
    }
    {
        MiniAsMacro *macro = find_macro(as, op);
        if (macro != NULL) {
            return expand_macro_invocation(as, macro, args, line);
        }
        /* GAS accepts a single macro argument next to the mnemonic as
         * MACRO(symbol), where the parentheses are LITERAL argument text.
         * Linux x86 STACK_FRAME_NON_STANDARD(clear_bhb_loop) uses exactly
         * this spelling. Do not strip the parentheses (GNU does not), or
         * turn an unknown ISA opcode with brackets into a macro. */
        if(args[0]=='\0' && op[0]!='.') {
            char *open=strchr(op,'(');
            size_t length=strlen(op);
            if(open!=NULL && open>op && length>1U &&
               op[length-1U]==')') {
                *open='\0';
                macro=find_macro(as,op);
                /* Registry lookup needs the temporary NUL, but GNU's
                 * actual operand starts at the ORIGINAL '(' character. */
                *open='(';
                if(macro!=NULL)
                    return expand_macro_invocation(as,macro,open,line);
            }
        }
    }

    if (strcmp(op, ".set") == 0 || strcmp(op, ".equ") == 0 ||
        strcmp(op, ".org") == 0) {
        as->layout_sensitive = true;
    }
    if ((strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
         strcmp(op, ".p2align") == 0 || strcmp(op, ".space") == 0 ||
         strcmp(op, ".skip") == 0 || strcmp(op, ".zero") == 0) &&
        strchr(args, '.') != NULL) {
        as->layout_sensitive = true;
    }
    /* GNU .req aliases are written as NAME .req REGISTER. Route this
     * generic target-directive spelling through the existing backend hook;
     * Common knows nothing about A64 alias names or register classes. */
    if(op[0]!='.' && !strncmp(args,".req",4U) &&
       (args[4]=='\0' || isspace((unsigned char)args[4])) &&
       as->target->parse_target_directive) {
        bool handled=false;
        if(!as->target->parse_target_directive(as,op,args,line,&handled))
            return false;
        if(handled)return true;
    }
    if (op[0] == '.') {
        bool handled = false;
        /* Target-specific directives are optional, unlike the mandatory
         * MCInst hooks. No-backend-specific-directive is a valid target. */
        if (as->target->parse_target_directive != NULL &&
            !as->target->parse_target_directive(as, op, args,
                                                 line, &handled)) {
            return false;
        }
        if (handled) return true;
        if (as->target->validate_common_directive != NULL &&
            !as->target->validate_common_directive(as, op, line))
            return false;
    }
    if (op[0] == '.' && strcmp(op, ".insn") != 0) {
        if (strcmp(op, ".cfi_sections") == 0 ||
            ((as->cfi_debug_frame || as->cfi_eh_frame) &&
             strncmp(op, ".cfi_", 5U) == 0) ||
            (as->target->elf_machine == 62U &&
             strncmp(op, ".cfi_", 5U) == 0)) {
            /* GNU x86 Linux VDSO/entry defaults to real .eh_frame CFI,
             * whereas the independent .cfi_sections .debug_frame test
             * keeps the original full-width CIE/FDE representation.
             * Preserve legacy non-x86 minimal boundary tracking until
             * each target gains separately validated DWARF semantics. */
            if (as->target->elf_machine == 62U &&
                !as->cfi_debug_frame && !as->cfi_eh_frame &&
                strcmp(op, ".cfi_sections") != 0)
                as->cfi_eh_frame = true;
            return minias_cfi_directive(as, op, args, line);
        }
        if (!strcmp(op,".file") || !strcmp(op,".loc"))
            return minias_debug_directive(as,op,args,line);
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
        if (strcmp(op, ".local") == 0) {
            return handle_symbol_list(as,args,MINIAS_STB_LOCAL,true,0U,false);
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
        if (strcmp(op, ".comm") == 0) {
            return parse_comm(as, args, line);
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
        if (strcmp(op, ".reloc") == 0) {
            return handle_named_reloc(as,args,line);
        }
        if (strcmp(op,".ltorg")==0 || strcmp(op,".pool")==0) {
            if(!as->target->supports_literal_pools || *args) {
                minias_set_error(as,"unsupported-directive:%s:line=%zu",
                                 op,line);
                return false;
            }
            as->layout_sensitive=true;
            return flush_literal_pools(as,line,false);
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
        if (strcmp(op,".altmacro")==0 || strcmp(op,".noaltmacro")==0) {
            if(*args!='\0') {
                minias_set_error(as,"bad-directive:%s:line=%zu",op,line);
                return false;
            }
            as->altmacro_active=strcmp(op,".altmacro")==0;
            as->layout_sensitive=true;
            return true;
        }
        if (strcmp(op, ".file") == 0 ||
            strcmp(op, ".ident") == 0 || strcmp(op, ".end") == 0) {
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
        if (data_width(as, op) != 0U || strcmp(op, ".fill") == 0 ||
            strcmp(op, ".zero") == 0 ||
            strcmp(op, ".space") == 0 || strcmp(op, ".skip") == 0 ||
            strcmp(op, ".uleb128") == 0 || strcmp(op, ".sleb128") == 0 ||
            strcmp(op, ".asciz") == 0 ||
            strcmp(op, ".string") == 0 || strcmp(op, ".ascii") == 0 ||
            strcmp(op, ".incbin") == 0 ||
            strcmp(op, ".uleb128") == 0 ||
            strcmp(op, ".sleb128") == 0) {
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
        const size_t statement_id = as->stmt_count;
        bool ok;
        if (!rewrite_numeric_local_refs(as, args, &rewritten_args, line))
            return false;
        /* ISA/ABI validation precedes the single target operand parse.
         * Layout asks the parsed instruction for its size only once. */
        if (as->target->validate_instruction != NULL &&
            !as->target->validate_instruction(
                as, op, rewritten_args, reason, sizeof(reason))) {
            minias_set_error(as, "%s:line=%zu", reason, line);
            free(rewritten_args);
            return false;
        }
        ok = add_stmt(as, MINIAS_STMT_INSN, op, rewritten_args,
                      line, 0U, 1U);
        if (ok)
            ok = as->target->measure_mcinst(
                as, &as->stmts[statement_id], &size);
        if (!ok) {
            free(rewritten_args);
            return false;
        }
        if (force_long_branch_for_stmt(as, statement_id)) {
            uint32_t grown = 0U;
            if (as->target->expanded_instruction_size == NULL ||
                !as->target->expanded_instruction_size(op, size, &grown) ||
                grown <= size) {
                minias_set_error(as, "invalid-target-relaxation:%s:line=%zu",
                                 op, line);
                free(rewritten_args);
                return false;
            }
            size = grown;
        }
        as->stmts[statement_id].size = size;
        free(rewritten_args);
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

static int classify_macro_line(const MiniAs *as, const char *text,
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
    strip_comment(copy, as->target);
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
    bool last_was_comma = false;

    if (!macro_has_top_level_comma(text)) {
        /* GNU GAS treats a one-parameter arithmetic macro invocation
         * such as 'nops (32 - 16) / 4' as ONE argument, not the 3
         * whitespace tokens of the generic .macro parameter lexer.
         * Preserve strict rejection of 'single 1 2' (no expression
         * operator) and keep comma-separated multi-arg GNU binding. */
        if(expected_count==1U && capacity!=0U) {
            char *whole=minias_trim(text);
            if(*whole && strpbrk(whole,"+-*/%&|^")!=NULL) {
                tokens[0]=whole;
                return 1U;
            }
        }
        return split_macro_tokens(text, tokens, capacity);
    }

    for (;;) {
        char *start;
        char *p;
        int depth = 0;
        char quote = '\0';

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            /* Explicit trailing comma is one more EMPTY positional
             * argument. Do not synthesize it without a delimiter. */
            if(last_was_comma) {
                if(segment_count==sizeof(segments)/sizeof(segments[0]))
                    return SIZE_MAX;
                segments[segment_count++]=cursor;
            }
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
        last_was_comma=*p==',';
        if(last_was_comma)*p++ = '\0';
        segments[segment_count++] = minias_trim(start);
        cursor = p;
        if(!last_was_comma)break;
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
            /* A GNU comma-delimited argument like
             *   ldr_l x0, symbol + 24
             * is ONE symbolic expression (the third 'tmp' is omitted),
             * not 'symbol' plus an invented tmp='+ 24'. Keep one Common
             * expression-aware lexical rule without ISA knowledge. */
            {
                char *rhs=space+1U,*lhs=space;
                while(*rhs==' ' || *rhs=='\t')++rhs;
                while(lhs>segment && (lhs[-1]==' ' || lhs[-1]=='\t'))
                    --lhs;
                if((*rhs && strchr("+-*/%&|^<>",*rhs)) ||
                   (lhs>segment && strchr("+-*/%&|^<>",lhs[-1])))
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
        /* Blank comma-delimited arguments occupy a slot. Critical for
         * real Linux GHASH __pmull_p8_tail ...,8b,,sh1,... where the
         * deliberately empty 't' suffix must NOT consume sh1 and turn
         * the ISA mnemonic into invalid 'pmullsh1'. */
        if (count == capacity) return SIZE_MAX;
        tokens[count++] = minias_trim(segment);
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
            if(!strcmp(qualifier,"vararg") &&
               i+1U==token_count && equal==NULL) {
                macro->params[i-1U].variadic=true;
            }else if(!strcmp(qualifier,"req")) {
                macro->params[i-1U].required=true;
            }else {
                minias_set_error(as,"unsupported-macro-qualifier:%s:line=%zu",
                                 qualifier,line);
                free(copy);
                return false;
            }
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
    char evaluated[64][32];
    const char *values[64];
    bool assigned[64]={false};
    char *vararg_buffer=NULL;
    size_t vararg_length=0U;
    bool has_vararg=macro->param_count>0U &&
           macro->params[macro->param_count-1U].variadic;
    size_t positional=0U;
    size_t supplied;
    MiniAsSourceLine *expanded = NULL;
    size_t i;
    size_t counter;
    /* .purgem or nested .macro can mutate the registry while executing
     * an already-expanded immutable source snapshot. Never dereference
     * the borrowed macro pointer after process_source_range returns. */
    size_t body_count=macro->body_count;
    bool ok = false;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:macro-arguments");
        return false;
    }
    supplied = split_macro_arguments(copy,
                                     tokens,
                                     64U,
                                     macro->param_count);
    if(supplied==SIZE_MAX || (!has_vararg && supplied>macro->param_count)) {
        minias_set_error(as, "bad-macro-arguments:%s:line=%zu",
                         macro->name, line);
        free(vararg_buffer);
        free(copy);
        return false;
    }
    /* GAS .altmacro evaluates %<absolute expression> at MACRO
     * invocation time. This is required by ARM64 fpsimdmacros.h's
     * recursive __for %\from, %((\from+\to)/2) generator; treating %0 as
     * a name changes control flow and silently bypasses generated ISA.
     * Conversion is bounded on-stack and happens BEFORE argument binding.
     * Modulo (%) inside a normal expression is never reinterpreted. */
    if(as->altmacro_active) {
        for(i=0U;i<supplied;++i) {
            int64_t value;
            const char *expression=tokens[i];
            if(*expression!='%')continue;
            ++expression;
            if(!*expression ||
               !evaluate_absolute_expression(as,expression,&value)) {
                minias_set_error(as,"bad-altmacro-percent:%s:line=%zu",
                                 tokens[i],line);
                free(copy);return false;
            }
            (void)snprintf(evaluated[i],sizeof(evaluated[i]),"%lld",
                           (long long)value);
            tokens[i]=evaluated[i];
        }
    }
    if(has_vararg) {
        vararg_buffer=malloc(strlen(args)+1U);
        if(!vararg_buffer) {
            minias_set_error(as,"out-of-memory:macro-vararg");
            free(vararg_buffer);free(copy);return false;
        }
        vararg_buffer[0]='\0';
    }
    /* GNU GAS named macro arguments can appear in any order:
     * .macro SAVE_ALL reg=0 docfi=0 / SAVE_ALL docfi=1,reg=7.
     * Do not substitute the literal string "docfi=1" for \\docfi,
     * since that silently changes .if 1 to .if docfi=1.
     * Positional and keyword argument binding share this Common owner. */
    for(i=0U;i<macro->param_count;++i)
        values[i]=macro->params[i].default_value ?
                  macro->params[i].default_value:"";
    for(i=0U;i<supplied;++i) {
        char *token=tokens[i],*equal=strchr(token,'=');
        size_t index;
        if(equal && equal!=token && equal[1]!='=' &&
           equal[-1]!='=') {
            bool matched=false;
            char *key;
            *equal='\0';
            key=minias_trim(token);
            for(index=0U;index<macro->param_count;++index)
                if(!strcmp(key,macro->params[index].name)) {
                    matched=true;break;
                }
            if(!matched || assigned[index]) {
                minias_set_error(as,"bad-macro-keyword:%s:%s:line=%zu",
                                 macro->name,key,line);
                free(vararg_buffer);free(copy);return false;
            }
            {
                const char *item=strip_macro_argument_quotes(
                                      minias_trim(equal+1U));
                /* Empty GNU named/positional arguments retain their
                 * declaration defaults. ARM64 __frame extra=0 is invoked
                 * through an omitted optional outer parameter. */
                if(*item || !macro->params[index].default_value)
                    values[index]=item;
            }
        }else if(has_vararg &&
                 positional>=macro->param_count-1U) {
            const char *item=strip_macro_argument_quotes(token);
            size_t len=strlen(item),max=strlen(args);
            index=macro->param_count-1U;
            if(assigned[index] && values[index]!=vararg_buffer) {
                minias_set_error(as,"duplicate-macro-vararg:%s:line=%zu",
                                 macro->name,line);
                free(vararg_buffer);free(copy);return false;
            }
            if(len>max-vararg_length-
                   (vararg_length!=0U?1U:0U)) {
                minias_set_error(as,"macro-vararg-too-long:line=%zu",line);
                free(vararg_buffer);free(copy);return false;
            }
            if(vararg_length)vararg_buffer[vararg_length++]=',';
            memcpy(vararg_buffer+vararg_length,item,len+1U);
            vararg_length+=len;
            values[index]=vararg_buffer;
            positional=macro->param_count;
        }else {
            while(positional<macro->param_count && assigned[positional])
                ++positional;
            if(positional>=macro->param_count) {
                minias_set_error(as,"too-many-macro-arguments:%s:line=%zu",
                                 macro->name,line);
                free(vararg_buffer);free(copy);return false;
            }
            index=positional++;
            {
                const char *item=strip_macro_argument_quotes(token);
                if(*item || !macro->params[index].default_value)
                    values[index]=item;
            }
        }
        assigned[index]=true;
    }
    for(i=0U;i<macro->param_count;++i)
        if((!assigned[i] || !*values[i]) &&
           macro->params[i].required) {
            minias_set_error(as,"missing-macro-argument:%s:%s:line=%zu",
                             macro->name,macro->params[i].name,line);
            free(vararg_buffer);free(copy);return false;
        }
    if (as->macro_expansion_depth >= 64U) {
        minias_set_error(as, "macro-expansion-depth:%s:line=%zu",
                         macro->name, line);
        free(vararg_buffer);
        free(copy);
        return false;
    }
    if (body_count != 0U) {
        expanded = calloc(body_count, sizeof(*expanded));
        if (expanded == NULL) {
            minias_set_error(as, "out-of-memory:macro-expanded-lines");
            free(copy);
            return false;
        }
    }
    counter = as->macro_expansion_counter++;
    for (i = 0U; i < body_count; ++i) {
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
    ok = process_source_range(as, expanded, 0U, body_count);
    --as->macro_expansion_depth;

done:
    destroy_source_lines(expanded, body_count);
    free(vararg_buffer);
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
        strip_gnu_c_comments(as,linebuf);
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", *line_no);
            destroy_source_lines(lines, count);
            return false;
        }
        kind = classify_macro_line(as, linebuf, argument, sizeof(argument));
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

static int classify_repeat_line(const MiniAs *as, const char *text,
                                char *argument,
                                size_t argument_size,
                                size_t *label_prefix_length) {
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
    strip_comment(copy, as->target);
    trimmed = minias_trim(copy);
    if(label_prefix_length)*label_prefix_length=0U;
    /* GNU allows LABEL: .irp/.rept ...; classify the opener after
     * its lexical labels, while preserving their source offset so
     * the shared label handler binds them once, BEFORE expansion. */
    {
        char *cursor=trimmed;
        for(;;) {
            char *end=cursor;
            while(*end && !isspace((unsigned char)*end) && *end!=':')
                ++end;
            if(*end!=':' || end==cursor)break;
            cursor=end+1U;
            while(isspace((unsigned char)*cursor))++cursor;
        }
        if(cursor!=trimmed) {
            if(label_prefix_length)
                *label_prefix_length=(size_t)(cursor-copy);
            trimmed=cursor;
        }
    }

    if (strncmp(trimmed, ".rept", 5U) == 0 &&
        (trimmed[5] == '\0' || isspace((unsigned char)trimmed[5]))) {
        rest = trimmed + 5;
        kind = MINIAS_REPEAT_REPT;
    } else if (strncmp(trimmed, ".rep", 4U) == 0 &&
               (trimmed[4] == '\0' || isspace((unsigned char)trimmed[4]))) {
        /* GNU .rep is the same text repetition directive as .rept.
         * Resolve only the lexical alias; Common IRP/repetition owns
         * balanced nesting, numeric evaluation and source fragments. */
        rest = trimmed + 4;
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

    if(label_prefix_length &&
       kind!=MINIAS_REPEAT_REPT && kind!=MINIAS_REPEAT_IRP)
        *label_prefix_length=0U;
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
    strip_comment(copy, as->target);
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

/* A .irp/.rept opener's leading label binds once at the
 * pre-expansion location, through Common's ordinary label parser. */
static bool emit_repeat_leading_label(MiniAs *as,const char *line,
                                      size_t prefix,size_t source_line) {
    char *copy;bool ok;
    if(prefix==0U)return true;
    if(prefix>strlen(line))return false;
    copy=malloc(prefix+1U);
    if(!copy){minias_set_error(as,"out-of-memory:repeat-leading-label");return false;}
    memcpy(copy,line,prefix);copy[prefix]='\0';
    ok=process_source_line(as,copy,source_line);
    free(copy);return ok;
}

static bool repeat_count_value(MiniAs *as,
                               const char *argument,
                               size_t line,
                               uint64_t *count) {
    int64_t value;
    char *rewritten = NULL;
    bool ok;

    /* GNU GAS permits 661b/662b local numeric labels in .rept counts,
     * including the kernel NOPS pattern (662b-661b)/4. Rewrite through
     * the same Common local-label owner as instructions and .byte; do
     * not clone target-specific numeric-label parsing. */
    if (!rewrite_numeric_local_refs(as, argument, &rewritten, line)) {
        return false;
    }
    ok = *rewritten != '\0' &&
         evaluate_absolute_expression(as, rewritten, &value) &&
         value >= 0;
    free(rewritten);
    if (!ok) {
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
        char *next_comma;
        char *next_space;
        char *next;
        char *value;
        bool comma_delimited;
        MiniAsSourceLine *expanded = NULL;
        size_t i;

        /* GNU GAS .irp accepts BOTH commas and unquoted whitespace
         * between values, even in the same list. Original Linux GHASH:
         *   .irp key, K0, K1, K2, K3, K4 K5
         * must expand K4 and K5 separately. Reuse the shared macro
         * whitespace lexer (which respects quotes and brackets). */
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        next_comma = strchr(cursor, ',');
        next_space = find_macro_argument_space(cursor);
        comma_delimited = next_comma != NULL &&
                          (next_space == NULL || next_comma < next_space);
        next = comma_delimited ? next_comma : next_space;
        if (next != NULL) *next = '\0';
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
        /* Do not manufacture a blank final argument from trailing
         * whitespace. A genuine trailing comma still carries an empty
         * GNU positional value, as it did before this change. */
        if (!comma_delimited) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor == '\0') break;
        }
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
            classify_macro_line(as, lines[i].text,
                                macro_argument,
                                sizeof(macro_argument));
        char argument[256];
        size_t repeat_label_prefix=0U;
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
                    classify_macro_line(as, lines[j].text,
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

        kind = classify_repeat_line(as, lines[i].text,
                                        argument,
                                        sizeof(argument),
                                        &repeat_label_prefix);
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
            if(!emit_repeat_leading_label(as,lines[i].text,
                                          repeat_label_prefix,
                                          lines[i].line))return false;

            while (j < end && depth != 0U) {
                char nested_argument[256];
                int nested_kind =
                    classify_repeat_line(as, lines[j].text,
                                         nested_argument,
                                         sizeof(nested_argument),NULL);
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
        strip_gnu_c_comments(as,linebuf);
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", *line_no);
            destroy_source_lines(lines, count);
            return false;
        }
        kind = classify_repeat_line(as, linebuf, argument,
                                    sizeof(argument),NULL);
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

/* GNU GAS permits semicolon-separated assignments and complete .rept/.irp
 * blocks on ONE physical source line (Linux head_64.S page tables).  Route
 * logical statements back through the existing Common repeat/macro engine;
 * do not recognize repeat syntax a second time in Target parsers. */
static bool process_inline_repeat_line(MiniAs *as,const char *source,
                                       size_t line,bool *handled) {
    MiniAsSourceLine *pieces=NULL;
    size_t count=0U,capacity=0U,i;
    char *copy,*cursor;
    bool opener=false,closer=false,ok=true;
    *handled=false;
    if(strchr(source,';')==NULL)return true;
    copy=minias_strdup(source);
    if(copy==NULL) {
        minias_set_error(as,"out-of-memory:inline-repeat");return false;
    }
    strip_comment(copy,as->target);
    cursor=copy;
    while(cursor!=NULL) {
        char *end=find_statement_separator(cursor);
        char argument[256];
        int kind;
        if(end!=NULL)*end='\0';
        kind=classify_repeat_line(as,cursor,argument,sizeof(argument),NULL);
        if(kind<0 || !grow_array((void **)&pieces,&capacity,
                                   sizeof(*pieces),count+1U)) {
            minias_set_error(as,"out-of-memory:inline-repeat-segments");
            ok=false;break;
        }
        pieces[count].text=cursor;
        pieces[count].line=line;
        ++count;
        if(kind==MINIAS_REPEAT_REPT || kind==MINIAS_REPEAT_IRP)opener=true;
        if(kind==MINIAS_REPEAT_ENDR)closer=true;
        cursor=end==NULL?NULL:end+1U;
    }
    if(ok && opener && closer) {
        *handled=true;
        ok=process_source_range(as,pieces,0U,count);
    }
    for(i=0U;i<count;++i)pieces[i].text=NULL; /* owned by copy */
    free(pieces);
    free(copy);
    return ok;
}

bool minias_parse_file(MiniAs *as, const char *path) {
    FILE *file = fopen(path, "r");
    char linebuf[65536];
    size_t line_no = 0U;

    if (file == NULL) {
        minias_set_error(as, "input-open:%s", path);
        return false;
    }
    as->c_comment_active=false;
    while (fgets(linebuf, sizeof(linebuf), file) != NULL) {
        size_t len;
        char argument[256];
        size_t repeat_label_prefix=0U;
        int kind;

        ++line_no;
        strip_gnu_c_comments(as,linebuf);
        len = strlen(linebuf);
        if (len == sizeof(linebuf) - 1U && linebuf[len - 1U] != '\n') {
            minias_set_error(as, "line-too-long:line=%zu", line_no);
            fclose(file);
            return false;
        }

        /* A complete inline repetition is lexed as logical statements
         * before physical-line .macro/.rept classification. */
        {
            bool consumed=false;
            if(!process_inline_repeat_line(as,linebuf,line_no,&consumed)) {
                fclose(file);return false;
            }
            if(consumed)continue;
        }

        {
            char macro_argument[1024];
            int macro_kind =
                classify_macro_line(as, linebuf,
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

        kind = classify_repeat_line(as, linebuf, argument,
                                    sizeof(argument),&repeat_label_prefix);
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
            if(!emit_repeat_leading_label(as,linebuf,repeat_label_prefix,
                                          opener_line)){
                fclose(file);return false;
            }

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
    if(as->c_comment_active) {
        minias_set_error(as,"unterminated-gnu-block-comment");
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
    if(as->target->supports_literal_pools &&
       !flush_literal_pools(as,line_no,true))return false;
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
            /* The offset invariant must fail CLOSED, but record the
             * exact preceding fragment to locate the real producer that
             * emitted > its measured size. Never hide overlap by
             * truncating the object or rewriting .org into a no-op. */
            const MiniAsStmt *previous=i!=0U?refs[i-1U].stmt:NULL;
            minias_set_error(as,
                "internal-offset:%s:line=%zu:expected=%llu:actual=%llu:"
                "previous=%s:previous_offset=%llu:previous_size=%u:"
                "previous_line=%zu:previous_source=%.35s",
                stmt->op,stmt->line,
                (unsigned long long)stmt->offset,
                (unsigned long long)section->size,
                previous?previous->op:"none",
                previous?(unsigned long long)previous->offset:0ULL,
                previous?previous->size:0U,
                previous?previous->line:0U,
                previous?previous->args:"none");
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
            bool exec_align = (section->flags & MINIAS_SHF_EXECINSTR) != 0U &&
                              strcmp(stmt->op, ".org") != 0;
            if (!(stmt->align_has_fill
                      ? emit_common_space(as,stmt->section,stmt->size,stmt->align_fill)
                      : exec_align
                      ? as->target->append_code_alignment(as, stmt->section,
                                                              stmt->size)
                      : minias_section_append_zero(as, stmt->section, stmt->size))) {
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
        } else if (!as->target->encode(as, stmt)) {
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
    return minias_cfi_emit(as) && minias_debug_emit(as);
}


int minias_assemble_config(const char *input_path,
                           const char *output_path,
                           const MiniAsAssemblyConfig *config,
                           FILE *diagnostic) {
    size_t *long_branch_ids = NULL;
    size_t long_branch_count = 0U;
    size_t long_branch_capacity = 0U;
    int rc = 1;

    for (;;) {
        MiniAs as;
        bool parsed;
        bool added_long_branches = false;

        minias_init(&as);
        as.target = config->target;
        as.elf_flags = config->elf_flags;
        as.strict_rv64i = config->strict_i_xlen != 0U ||
                          config->isa_explicit;
        as.strict_i_xlen = config->strict_i_xlen;
        as.isa_explicit = config->isa_explicit;
        as.target_xlen = config->elf32 ? 32U : 64U;
        as.isa_features = config->isa_features;
        as.target_attributes_size = config->target_attributes_size;
        if (as.target_attributes_size > sizeof(as.target_attributes)) {
            minias_set_error(&as, "target-attribute-too-large");
        } else if (as.target_attributes_size != 0U) {
            memcpy(as.target_attributes, config->target_attributes,
                   as.target_attributes_size);
        }
        as.forced_long_branch_ids = long_branch_ids;
        as.forced_long_branch_count = long_branch_count;
        parsed = as.error[0] == '\0' && minias_parse_file(&as, input_path);
        if (parsed && !minias_resolve_deferred_data(&as))parsed=false;
        if (parsed &&
            !collect_out_of_range_branches(&as,
                                           &long_branch_ids,
                                           &long_branch_count,
                                           &long_branch_capacity,
                                           &added_long_branches)) {
            parsed = false;
        }
        if (parsed && added_long_branches && !as.layout_sensitive) {
            size_t passes = 0U;
            do {
                size_t j;
                for (j = 0U; j < long_branch_count; ++j) {
                    size_t id = long_branch_ids[j];
                    MiniAsStmt *fragment;
                    uint32_t grown;
                    if (id >= as.stmt_count) {
                        minias_set_error(&as, "internal:relaxation-fragment-id");
                        parsed = false;
                        break;
                    }
                    fragment = &as.stmts[id];
                    grown = fragment->size;
                    if (as.target->expanded_instruction_size == NULL) {
                        minias_set_error(&as, "missing-target-fragment-growth:%s",
                                         fragment->op);
                        parsed = false;
                        break;
                    }
                    /* Every target reports its OWN initial instruction size:
                     * RISC-V grows 4->8; x86 grows 2->5/6. Never silently
                     * assume the common fragment must start at 4 bytes. */
                    if (as.target->expanded_instruction_size(
                            fragment->op, fragment->size, &grown)) {
                        if (grown <= fragment->size) {
                            minias_set_error(&as, "invalid-target-fragment-growth:%s",
                                             fragment->op);
                            parsed = false;
                            break;
                        }
                        fragment->size = grown;
                    } else {
                        uint32_t needed = fragment->size;
                        if (as.target->relax_instruction == NULL ||
                            !as.target->relax_instruction(&as, fragment, &needed) ||
                            needed > fragment->size) {
                            minias_set_error(&as, "unrelaxed-target-fragment:%s",
                                             fragment->op);
                            parsed = false;
                            break;
                        }
                    }
                }
                if (!parsed || !minias_relayout_fragments(&as) ||
                    !collect_out_of_range_branches(&as, &long_branch_ids,
                                                    &long_branch_count,
                                                    &long_branch_capacity,
                                                    &added_long_branches)) {
                    parsed = false;
                    break;
                }
                if (++passes > as.stmt_count + 1U) {
                    minias_set_error(&as, "fragment-relaxation-nonconvergent");
                    parsed = false;
                    break;
                }
            } while (added_long_branches);
            if (parsed && diagnostic != NULL &&
                getenv("MINIAS_TRACE_RELAYOUT") != NULL) {
                fprintf(diagnostic,
                        "minias-fragment-relayout:inplace-passes=%zu\n", passes);
            }
        }
        if (parsed && added_long_branches) {
            minias_destroy(&as);
            continue;
        }
        if (parsed && as.error[0] == '\0' &&
            minias_emit_sections(&as) &&
            as.target->validate_output(&as) &&
            (config->elf32 ? minias_write_elf32(&as, output_path)
                   : minias_write_elf64(&as, output_path))) {
            rc = 0;
        }
        if (rc != 0 && diagnostic != NULL) {
            fprintf(diagnostic,
                    "minic-as: %s\n",
                    as.error[0] == '\0' ? "unknown-error" : as.error);
        }
        minias_destroy(&as);
        break;
    }
    free(long_branch_ids);
    return rc;
}

int minias_assemble_with_target(const char *input_path,
                                const char *output_path,
                                const MiniAsTargetOps *target,
                                bool elf32,
                                uint32_t elf_flags,
                                unsigned strict_i_xlen,
                                FILE *diagnostic) {
    MiniAsAssemblyConfig config;
    memset(&config, 0, sizeof(config));
    config.target = target;
    config.elf32 = elf32;
    config.elf_flags = elf_flags;
    config.strict_i_xlen = strict_i_xlen;
    return minias_assemble_config(input_path, output_path, &config, diagnostic);
}

int minias_assemble_file_target_isa(const char *input_path,
                                    const char *output_path,
                                    bool elf32,
                                    uint32_t elf_flags,
                                    unsigned strict_i_xlen,
                                    FILE *diagnostic) {
    return minias_assemble_with_target(input_path, output_path,
                                       minias_target_riscv(), elf32,
                                       elf_flags, strict_i_xlen, diagnostic);
}

/* Historical API stays permissive: only explicit -march=rv64i activates
 * ISA qualification so the existing MiniAS/Linux acceptance does not change. */
int minias_assemble_file_target(const char *input_path,
                                const char *output_path,
                                bool elf32,
                                uint32_t elf_flags,
                                FILE *diagnostic) {
    return minias_assemble_file_target_isa(
        input_path, output_path, elf32, elf_flags, false, diagnostic);
}

int minias_assemble_file_class(const char *input_path,
                               const char *output_path,
                               bool elf32,
                               FILE *diagnostic) {
    return minias_assemble_file_target(input_path,
                                       output_path,
                                       elf32,
                                       0U,
                                       diagnostic);
}

int minias_assemble_file(const char *input_path,
                         const char *output_path,
                         FILE *diagnostic) {
    return minias_assemble_file_class(input_path, output_path, false, diagnostic);
}
