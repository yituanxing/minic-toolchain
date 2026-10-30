#include "minias_internal.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static bool validate_user_source_private_marker(MiniAs *as,
                                                const char *line,
                                                size_t source_line);
static void source_graph_record_condition(MiniAs *as,
                                          MiniAsStableSiteId owner,
                                          bool active);
static bool source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              size_t *minimum_size);

bool minias_grow_array(void **ptr, size_t *capacity, size_t elem_size, size_t need) {
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

bool minias_current_location(MiniAs *as, uint64_t *value) {
    MiniAsSubsection *sub;

    if (as->current_section < 0 ||
        (size_t)as->current_section >= as->section_count) {
        minias_set_error(as, "internal:bad-current-section");
        return false;
    }
    sub = minias_ensure_subsection(as, as->current_section, as->current_subsection);
    if (sub == NULL) {
        return false;
    }
    *value = sub->size;
    return true;
}

static bool advance_current_location(MiniAs *as, uint64_t amount) {
    MiniAsSubsection *sub =
        minias_ensure_subsection(as, as->current_section, as->current_subsection);

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

/* SymbolID edges remain retained across every MCFragment relayout. Resolve
 * one edge only after its target has been normalized in the current pass.
 * This has no ISA knowledge: section-relative aliases and section-cancelled
 * absolute aliases share the same dependency graph and overflow contract. */
static bool resolve_symbol_alias_edge(MiniAs *as, size_t alias_id) {
    MiniAsSymbol *alias = &as->symbols[alias_id];
    const MiniAsSymbol *target = &as->symbols[alias->alias_target_index];
    uint64_t value = target->value;

    if (alias->alias_section_base != NULL) {
        int target_section = minias_find_section(as, alias->alias_section_base);
        int64_t absolute;
        if (target_section < 0 || target->section != target_section ||
            value > INT64_MAX ||
            !minias_checked_add_i64((int64_t)value,
                                     alias->alias_addend, &absolute)) {
            minias_set_error(as, "invalid-section-relative-set:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        alias->defined = true;
        alias->section = MINIAS_SECTION_ABS;
        alias->subsection = 0U;
        alias->value = (uint64_t)absolute;
        alias->type = MINIAS_STT_NOTYPE;
        alias->alias_pending = false;
        return true;
    }
    if (alias->alias_addend >= 0) {
        uint64_t addend = (uint64_t)alias->alias_addend;
        if (addend > UINT64_MAX - value) {
            minias_set_error(as, "set-value-overflow:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        value += addend;
    } else {
        uint64_t magnitude = (uint64_t)(-(alias->alias_addend + 1)) + 1U;
        if (magnitude > value) {
            minias_set_error(as, "set-value-underflow:%s:line=%zu",
                             alias->name, alias->alias_line);
            return false;
        }
        value -= magnitude;
    }
    alias->defined = true;
    alias->section = target->section;
    alias->subsection = target->subsection;
    alias->value = value;
    alias->type = target->type;
    if (alias->size == 0U) alias->size = target->size;
    alias->alias_pending = false;
    return true;
}

/* Iterative topological evaluation, O(symbols + alias edges). The former
 * repeated full-symbol scan was O(symbols * maximum alias-chain depth) and
 * ran AGAIN for every branch/deferred-data relayout pass. A 4096-entry GNU
 * macro alias chain could therefore dominate assembling a whole C file.
 *
 * States belong to this evaluation pass, not the parser: each later MC
 * relayout invalidates every dependent value and evaluates the same graph
 * against new symbol anchors. The explicit DFS path bounds stack usage and
 * rejects cycles/undefined targets without recursive C call depth. */
bool minias_refresh_symbol_aliases(MiniAs *as) {
    const size_t count = as->symbol_count;
    unsigned char *state;
    size_t *path;
    size_t i;
    bool result = false;

    if (count == 0U) return true;
    if (count > SIZE_MAX / sizeof(*path)) {
        minias_set_error(as, "symbol-alias-graph-capacity-overflow");
        return false;
    }
    state = calloc(count, sizeof(*state));
    path = malloc(count * sizeof(*path));
    if (state == NULL || path == NULL) {
        minias_set_error(as, "out-of-memory:symbol-alias-graph");
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        if (as->symbols[i].has_alias_dependency)
            as->symbols[i].alias_pending = true;
    }
    for (i = 0U; i < count; ++i) {
        size_t cursor = i;
        size_t depth = 0U;
        if (!as->symbols[i].has_alias_dependency || state[i] == 2U)
            continue;
        while (as->symbols[cursor].has_alias_dependency &&
               state[cursor] != 2U) {
            const MiniAsSymbol *alias = &as->symbols[cursor];
            if (alias->alias_target_index >= count) {
                minias_set_error(as, "internal:set-target-index:%s", alias->name);
                goto done;
            }
            if (state[cursor] == 1U) {
                minias_set_error(as, "unresolved-set-target:%s:%s:line=%zu",
                                 alias->name,
                                 as->symbols[alias->alias_target_index].name,
                                 alias->alias_line);
                goto done;
            }
            state[cursor] = 1U;
            path[depth++] = cursor;
            cursor = alias->alias_target_index;
        }
        if (!as->symbols[cursor].defined ||
            (as->symbols[cursor].has_alias_dependency &&
             state[cursor] != 2U) || as->symbols[cursor].alias_pending) {
            const MiniAsSymbol *alias = &as->symbols[path[depth-1U]];
            minias_set_error(as, "unresolved-set-target:%s:%s:line=%zu",
                             alias->name, as->symbols[cursor].name,
                             alias->alias_line);
            goto done;
        }
        while (depth != 0U) {
            size_t id = path[--depth];
            if (!resolve_symbol_alias_edge(as, id)) goto done;
            state[id] = 2U;
        }
    }
    result = true;
done:
    free(path);
    free(state);
    return result;
}

char *minias_strdup(const char *text) {
    size_t n = strlen(text) + 1U;
    char *copy = malloc(n);

    if (copy != NULL) {
        memcpy(copy, text, n);
    }
    return copy;
}

bool minias_is_private_symbol_name(const char *name) {
    return name!=NULL && (unsigned char)name[0]==1U &&
           strncmp(name+1,"minias_",7U)==0;
}

bool minias_make_private_symbol_name(char *out,size_t capacity,
                                     const char *kind,size_t ordinal) {
    size_t i;
    int n;
    if(out==NULL || capacity==0U || kind==NULL || *kind=='\0')return false;
    for(i=0U;kind[i]!='\0';++i) {
        if(!((kind[i]>='a' && kind[i]<='z') ||
             (kind[i]>='A' && kind[i]<='Z') ||
             (kind[i]>='0' && kind[i]<='9') || kind[i]=='_'))
            return false;
    }
    n=snprintf(out,capacity,"\001minias_%s_%zu",kind,ordinal);
    return n>0 && (size_t)n<capacity;
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

/* A SectionKey includes the whole GNU ELF grouping identity; name alone
 * is not unique for COMDAT or SHF_LINK_ORDER. SectionIDs never change even
 * when the Section array grows. This open-addressed index has independent
 * lifetime from ELF writer's ordering index and preserves first definition. */
static uint64_t section_key_hash_component(uint64_t hash,const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    hash^=text==NULL ? UINT64_C(0) : UINT64_C(1);
    hash*=UINT64_C(1099511628211);
    if(text!=NULL) {
        while(*p!='\0') {
            hash^=(uint64_t)*p++;
            hash*=UINT64_C(1099511628211);
        }
    }
    hash^=UINT64_C(255); /* tuple boundary */
    return hash*UINT64_C(1099511628211);
}

static size_t section_key_hash(const char *name,const char *group,
                               const char *link_order) {
    uint64_t hash=UINT64_C(1469598103934665603);
    hash=section_key_hash_component(hash,name);
    hash=section_key_hash_component(hash,group);
    hash=section_key_hash_component(hash,link_order);
    return (size_t)hash;
}

static bool section_key_component_equal(const char *a,const char *b) {
    return a==b || (a!=NULL && b!=NULL && strcmp(a,b)==0);
}

static bool section_key_equal(const MiniAsSection *sec,const char *name,
                              const char *group,const char *link_order) {
    return section_key_component_equal(sec->name,name) &&
           section_key_component_equal(sec->group_signature,group) &&
           section_key_component_equal(sec->link_order_reference,link_order);
}

/* Rehash only on growth, never on lookup. On OOM the original index remains
 * intact, and the pending new Section is not committed. */
static bool minias_rebuild_section_index(MiniAs *as,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    size_t i;
    if(slots==NULL) {
        minias_set_error(as,"out-of-memory:section-index");
        return false;
    }
    for(i=0U;i<as->section_count;++i) {
        const MiniAsSection *sec=&as->sections[i];
        size_t slot=section_key_hash(sec->name,sec->group_signature,
                                      sec->link_order_reference)&(capacity-1U);
        while(slots[slot]!=0U)slot=(slot+1U)&(capacity-1U);
        slots[slot]=i+1U;
    }
    free(as->section_slots);
    as->section_slots=slots;
    as->section_slot_capacity=capacity;
    return true;
}

static bool minias_reserve_section_index(MiniAs *as) {
    const size_t cap=as->section_slot_capacity;
    size_t next;
    if(cap!=0U && as->section_count+1U<cap/2U)return true;
    if(cap>SIZE_MAX/2U) {
        minias_set_error(as,"section-index-too-large");
        return false;
    }
    next=cap==0U ? 16U : cap*2U;
    return minias_rebuild_section_index(as,next);
}

static size_t minias_section_index_slot(const MiniAs *as,const char *name,
                                         const char *group,
                                         const char *link_order) {
    size_t slot=section_key_hash(name,group,link_order)&
                (as->section_slot_capacity-1U);
    /* At most half of the table is occupied. An empty slot always exists. */
    while(as->section_slots[slot]!=0U) {
        const size_t section_id=as->section_slots[slot]-1U;
        if(section_key_equal(&as->sections[section_id],name,group,link_order))
            break;
        slot=(slot+1U)&(as->section_slot_capacity-1U);
    }
    return slot;
}

static int minias_find_section_key(const MiniAs *as,const char *name,
                                   const char *group_signature,
                                   const char *link_order_reference) {
    size_t slot;
    if(as->section_slot_capacity==0U)return -1;
    slot=minias_section_index_slot(as,name,group_signature,
                                   link_order_reference);
    return as->section_slots[slot]==0U ? -1 :
           (int)(as->section_slots[slot]-1U);
}

int minias_find_section(const MiniAs *as,const char *name) {
    return minias_find_section_key(as,name,NULL,NULL);
}

static int minias_ensure_section_key(MiniAs *as,const char *name,
                                     const char *group_signature,
                                     const char *link_order_reference,
                                     uint32_t type,uint64_t flags,
                                     uint64_t align) {
    const int found=minias_find_section_key(as,name,group_signature,
                                             link_order_reference);
    MiniAsSection *sec;
    size_t slot;
    if(found>=0) {
        sec=&as->sections[(size_t)found];
        /* Preserve GAS first declaration; later conflicting metadata must
         * never silently change ELF section type or discard stored bytes. */
        (void)type;
        (void)flags;
        if(align>sec->align)sec->align=align;
        return found;
    }
    if(as->section_count>=(size_t)INT_MAX) {
        minias_set_error(as,"section-count-too-large");
        return -1;
    }
    if(!minias_reserve_section_index(as))return -1;
    if(!minias_grow_array((void **)&as->sections,&as->section_capacity,
                          sizeof(*as->sections),as->section_count+1U)) {
        minias_set_error(as,"out-of-memory:section");
        return -1;
    }
    sec=&as->sections[as->section_count];
    memset(sec,0,sizeof(*sec));
    sec->name=minias_strdup(name);
    if(sec->name==NULL) {
        minias_set_error(as,"out-of-memory:section-name");
        return -1;
    }
    if(group_signature!=NULL) {
        sec->group_signature=minias_strdup(group_signature);
        if(sec->group_signature==NULL) {
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:group-signature");
            return -1;
        }
    }
    if(link_order_reference!=NULL) {
        sec->link_order_reference=minias_strdup(link_order_reference);
        if(sec->link_order_reference==NULL) {
            free(sec->group_signature);sec->group_signature=NULL;
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:link-order-reference");
            return -1;
        }
    }
    sec->type=type;
    sec->flags=flags;
    sec->align=align==0U ? 1U : align;
    /* No failure point remains: publish SectionID and hash entry together. */
    slot=minias_section_index_slot(as,name,group_signature,
                                   link_order_reference);
    as->section_slots[slot]=as->section_count+1U;
    ++as->section_count;
    return (int)(as->section_count-1U);
}

int minias_ensure_section(MiniAs *as,
                          const char *name,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t align) {
    return minias_ensure_section_key(as,name,NULL,NULL,type,flags,align);
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
    if (!minias_grow_array((void **)&as->symbols,
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

bool minias_define_target_mapping_symbol(MiniAs *as, const char *stem,
                                         size_t first_ordinal,
                                         bool always_suffix,
                                         int section, uint64_t offset) {
    char name[640];
    size_t ordinal = first_ordinal;
    MiniAsSymbol *symbol;
    if (stem == NULL || *stem == '\0' || section < 0 ||
        (size_t)section >= as->section_count) {
        minias_set_error(as, "invalid-target-mapping-symbol");
        return false;
    }
    for (;;) {
        int written = (!always_suffix && ordinal == 0U)
            ? snprintf(name, sizeof(name), "%s", stem)
            : snprintf(name, sizeof(name), "%s.%zu", stem, ordinal);
        if (written < 0 || (size_t)written >= sizeof(name)) {
            minias_set_error(as, "target-mapping-symbol-name-overflow");
            return false;
        }
        if (minias_get_symbol(as, name, false) == NULL) break;
        if (ordinal == SIZE_MAX) {
            minias_set_error(as, "target-mapping-symbol-ordinal-overflow");
            return false;
        }
        ++ordinal;
    }
    symbol = minias_get_symbol(as, name, true);
    if (symbol == NULL) return false;
    symbol->defined = true;
    symbol->section = section;
    symbol->value = offset;
    symbol->bind = MINIAS_STB_LOCAL;
    symbol->type = MINIAS_STT_NOTYPE;
    return true;
}

/* Stable parse-time NameIds are intentionally NOT Object SymbolIDs.  A
 * machine operand may name a forward or external symbol during parsing,
 * while the historical ELF symbol order is determined later by definitions
 * and committed fixups.  Interning here may allocate only this name pool;
 * it must never call minias_get_symbol(...,true). */
static bool rebuild_symbol_name_index(MiniAs *as,size_t minimum) {
    size_t capacity=16U,i;
    size_t *slots;
    while(capacity<minimum) {
        if(capacity>SIZE_MAX/2U) {
            minias_set_error(as,"symbol-name-index-too-large");
            return false;
        }
        capacity*=2U;
    }
    slots=calloc(capacity,sizeof(*slots));
    if(!slots) {
        minias_set_error(as,"out-of-memory:symbol-name-index");
        return false;
    }
    for(i=0U;i<as->symbol_name_count;++i) {
        size_t slot=symbol_name_hash(as->symbol_names[i].name)&(capacity-1U);
        while(slots[slot]!=0U)slot=(slot+1U)&(capacity-1U);
        slots[slot]=i+1U;
    }
    free(as->symbol_name_slots);
    as->symbol_name_slots=slots;
    as->symbol_name_slot_capacity=capacity;
    return true;
}

bool minias_symbol_name_intern(MiniAs *as,const char *name,
                               MiniAsSymbolNameId *name_id) {
    size_t slot,scanned;
    if(as==NULL || name==NULL || *name=='\0' || name_id==NULL)return false;
    /* A repeated operand must not trigger a table expansion merely because
     * the pool is near its load threshold.  Name lookup has no side effect. */
    if(as->symbol_name_slot_capacity!=0U) {
        slot=symbol_name_hash(name)&(as->symbol_name_slot_capacity-1U);
        for(scanned=0U;scanned<as->symbol_name_slot_capacity;++scanned) {
            size_t encoded=as->symbol_name_slots[slot];
            if(encoded==0U)break;
            if(strcmp(as->symbol_names[encoded-1U].name,name)==0) {
                *name_id=encoded-1U;
                return true;
            }
            slot=(slot+1U)&(as->symbol_name_slot_capacity-1U);
        }
    }
    if(as->symbol_name_count==SIZE_MAX) {
        minias_set_error(as,"symbol-name-count-overflow");
        return false;
    }
    if(as->symbol_name_slot_capacity==0U ||
       as->symbol_name_count+1U>=as->symbol_name_slot_capacity/2U) {
        size_t next=as->symbol_name_slot_capacity==0U?16U:
                    as->symbol_name_slot_capacity*2U;
        if(next<as->symbol_name_slot_capacity ||
           !rebuild_symbol_name_index(as,next))return false;
    }
    slot=symbol_name_hash(name)&(as->symbol_name_slot_capacity-1U);
    for(scanned=0U;scanned<as->symbol_name_slot_capacity;++scanned) {
        size_t encoded=as->symbol_name_slots[slot];
        if(encoded==0U)break;
        slot=(slot+1U)&(as->symbol_name_slot_capacity-1U);
    }
    if(scanned==as->symbol_name_slot_capacity ||
       as->symbol_name_count==SIZE_MAX ||
       !minias_grow_array((void **)&as->symbol_names,
                          &as->symbol_name_capacity,
                          sizeof(*as->symbol_names),
                          as->symbol_name_count+1U)) {
        minias_set_error(as,"out-of-memory:symbol-name-pool");
        return false;
    }
    as->symbol_names[as->symbol_name_count].name=minias_strdup(name);
    if(as->symbol_names[as->symbol_name_count].name==NULL) {
        minias_set_error(as,"out-of-memory:symbol-name");
        return false;
    }
    *name_id=as->symbol_name_count;
    as->symbol_name_slots[slot]=++as->symbol_name_count;
    return true;
}

bool minias_bind_symbol_ref(MiniAs *as,const MiniAsSymbolExpr *expr,
                            MiniAsSymbolRef *ref) {
    MiniAsSymbolNameId name_id;
    if(expr==NULL || ref==NULL ||
       !minias_symbol_name_intern(as,expr->name,&name_id))return false;
    ref->name_id=name_id;
    ref->addend=expr->addend;
    return true;
}

bool minias_bind_symbol_difference_ref(MiniAs *as,
                                        const MiniAsSymbolDifference *expr,
                                        MiniAsSymbolDifferenceRef *ref) {
    if(expr==NULL || ref==NULL)return false;
    return minias_bind_symbol_ref(as,&expr->positive,&ref->positive) &&
           minias_bind_symbol_ref(as,&expr->negative,&ref->negative);
}

const MiniAsSymbol *minias_symbol_ref_lookup(const MiniAs *as,
                                             MiniAsSymbolRef ref) {
    size_t slot=0U,index=0U;
    const char *name;
    if(as==NULL || ref.name_id>=as->symbol_name_count)return NULL;
    name=as->symbol_names[ref.name_id].name;
    if(name==NULL || !find_symbol_slot(as,name,&slot,&index))return NULL;
    return &as->symbols[index];
}

/* Keep staged MCData Fixup widths identical to the committed Section
 * contract. LoongArch uses 8-bit and 24-bit ADD/SUB pairs as well as
 * 16/32/64-bit relocations; staging must not reject a legal typed Fixup. */
static bool minias_fixup_field_width(unsigned width) {
    return width==1U || width==2U || width==3U ||
           width==4U || width==8U;
}

static bool minias_record_fixup(MiniAs *as,
                      int section,
                      uint64_t offset,
                      MiniAsFixupKind kind,
                      unsigned width,
                      const char *symbol_name,
                      size_t existing_symbol_index,
                      int64_t addend,
                      MiniAsFixupOrigin origin,bool absolute_zero_symbol) {
    MiniAsSymbol *symbol=NULL;
    MiniAsFixup *fixup;
    size_t symbol_index;
    uint32_t relative_offset = 0U;
    size_t fragment_id = SIZE_MAX;
    if (origin == MINIAS_FIXUP_ORIGIN_FRAGMENT) {
        const MiniAsStmt *owner;
        uint64_t delta;
        if (!as->emitting_fragment ||
            as->emitting_fragment_id >= as->stmt_count) {
            minias_set_error(as, "fixup-without-fragment-owner");
            return false;
        }
        fragment_id = as->emitting_fragment_id;
        owner = &as->stmts[fragment_id];
        if (owner->section != section || offset < owner->offset) {
            minias_set_error(as, "fixup-fragment-section-mismatch:line=%zu",
                             owner->line);
            return false;
        }
        delta = offset - owner->offset;
        if (delta > UINT32_MAX || delta > owner->size ||
            width > owner->size - delta) {
            minias_set_error(as, "fixup-outside-fragment:line=%zu",
                             owner->line);
            return false;
        }
        relative_offset = (uint32_t)delta;
    } else if ((origin != MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION &&
                origin != MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION) ||
               as->emitting_fragment) {
        minias_set_error(as, "invalid-section-fixup-owner");
        return false;
    }
    /* Keep semantic fixups typed until finalization. The object builder
     * must decide whether this field needs an ELF relocation at all. */
    if (section < 0 || (size_t)section >= as->section_count ||
        as->sections[(size_t)section].type==MINIAS_SHT_NOBITS ||
        !minias_fixup_field_width(width) ||
        offset > UINT64_MAX - width) {
        minias_set_error(as, "invalid-fixup-range:%s",
                         symbol_name != NULL ? symbol_name : "<symbol-id>");
        return false;
    }
    if(absolute_zero_symbol) {
        if(origin!=MINIAS_FIXUP_ORIGIN_FRAGMENT || symbol_name!=NULL ||
           existing_symbol_index!=SIZE_MAX || addend!=0) {
            minias_set_error(as,"invalid-abs0-fixup-owner");
            return false;
        }
        symbol_index=SIZE_MAX;
    } else if (existing_symbol_index != SIZE_MAX) {
        if (existing_symbol_index >= as->symbol_count) {
            minias_set_error(as, "invalid-fixup-symbol-id:%zu",
                             existing_symbol_index);
            return false;
        }
        symbol = &as->symbols[existing_symbol_index];
    } else {
        if (symbol_name == NULL) {
            minias_set_error(as, "missing-fixup-symbol");
            return false;
        }
        symbol = minias_get_symbol(as, symbol_name, true);
    }
    if (!absolute_zero_symbol && symbol == NULL) return false;
    if (!absolute_zero_symbol)symbol_index = (size_t)(symbol - as->symbols);
    if (!absolute_zero_symbol && !symbol->defined && symbol->bind == MINIAS_STB_LOCAL &&
        !symbol->explicit_local_binding &&
        strncmp(symbol->name, ".L", 2U) != 0)
        symbol->bind = MINIAS_STB_GLOBAL;

    if (!minias_grow_array((void **)&as->fixups,
                    &as->fixup_capacity,
                    sizeof(*as->fixups),
                    as->fixup_count + 1U)) {
        minias_set_error(as, "out-of-memory:fixup");
        return false;
    }
    fixup = &as->fixups[as->fixup_count++];
    fixup->section = section;
    fixup->origin = origin;
    fixup->fragment_id = fragment_id;
    fixup->fragment_offset = relative_offset;
    fixup->offset = origin == MINIAS_FIXUP_ORIGIN_FRAGMENT ? 0U : offset;
    fixup->kind = kind;
    fixup->width = width;
    fixup->symbol_index = symbol_index;
    fixup->addend = addend;
    fixup->prefer_section_symbol = false;
    return true;
}

/* MCData fixups used to bypass the instruction transaction and publish
 * Section-owned symbol references as the DATA loop ran. Stage them under the
 * same persistent source Fragment, keeping the original SymbolID or a
 * stable NameId; the commit phase alone creates final Fixup records. */
static bool minias_stage_common_fixup(MiniAs *as,int section,uint64_t offset,
                                      MiniAsFixupKind kind,unsigned width,
                                      const char *name,size_t symbol_index,
                                      int64_t addend) {
    MiniAsEncodeResult *result=as->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsEncodeFixupRequest *request;
    MiniAsSymbolNameId name_id=0U;
    uint64_t relative;
    if(result==NULL || !as->emitting_fragment ||
       as->emitting_fragment_id>=as->stmt_count) {
        minias_set_error(as,"mcdata-fixup-without-fragment");
        return false;
    }
    owner=&as->stmts[as->emitting_fragment_id];
    if(owner->section!=section || section<0 ||
       (size_t)section>=as->section_count ||
       as->sections[(size_t)section].type==MINIAS_SHT_NOBITS ||
       offset<owner->offset || !minias_fixup_field_width(width) ||
       (name==NULL && symbol_index>=as->symbol_count)) {
        minias_set_error(as,"invalid-mcdata-fixup:line=%zu",owner->line);
        return false;
    }
    relative=offset-owner->offset;
    if(relative>UINT32_MAX || relative>owner->size ||
       width>owner->size-relative ||
       result->fixup_count==SIZE_MAX) {
        minias_set_error(as,"mcdata-fixup-outside-fragment:line=%zu",owner->line);
        return false;
    }
    if(name!=NULL && !minias_symbol_name_intern(as,name,&name_id))
        return false;
    if(!minias_grow_array((void **)&result->fixups,
                          &result->fixup_capacity,sizeof(*result->fixups),
                          result->fixup_count+1U)) {
        minias_set_error(as,"out-of-memory:mcdata-fixup");
        return false;
    }
    request=&result->fixups[result->fixup_count++];
    memset(request,0,sizeof(*request));
    request->section=section;
    request->fragment_offset=(uint32_t)relative;
    request->kind=kind;
    request->width=width;
    request->has_symbol_index=name==NULL;
    request->symbol_index=symbol_index;
    request->symbol_ref.name_id=name_id;
    request->symbol_ref.addend=addend;
    return true;
}

bool minias_add_fixup(MiniAs *as, int section, uint64_t offset,
                      MiniAsFixupKind kind, unsigned width,
                      const char *symbol_name, int64_t addend) {
    if(as->emitting_fragment && !as->committing_fragment)
        return minias_stage_common_fixup(as,section,offset,kind,width,
                                          symbol_name,SIZE_MAX,addend);
    return minias_record_fixup(as, section, offset, kind, width,
                               symbol_name, SIZE_MAX, addend,
                               MINIAS_FIXUP_ORIGIN_FRAGMENT,false);
}

/* Fixed-width MCData already retains an actual SymbolID from its lexical
 * parse. Preserve that identity through emission, including the shared
 * undefined-local binding promotion and original Fixup append order. */
bool minias_add_fixup_symbol_id(MiniAs *as, int section, uint64_t offset,
                                MiniAsFixupKind kind, unsigned width,
                                size_t symbol_index, int64_t addend) {
    if(as->emitting_fragment && !as->committing_fragment)
        return minias_stage_common_fixup(as,section,offset,kind,width,
                                          NULL,symbol_index,addend);
    return minias_record_fixup(as, section, offset, kind, width,
                               NULL, symbol_index, addend,
                               MINIAS_FIXUP_ORIGIN_FRAGMENT,false);
}

bool minias_add_section_fixup(MiniAs *as, MiniAsFixupOrigin origin,
                             int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend) {
    return minias_record_fixup(as, section, offset, kind, width,
                               symbol_name, SIZE_MAX, addend, origin,false);
}

static bool minias_section_append_fill(MiniAs *,int,size_t,uint8_t,bool);
static bool emit_common_pattern(MiniAs *,int,const unsigned char *,size_t,size_t);
static bool minias_stage_fragment_bytes(MiniAs *,int,const void *,size_t,bool);

/* Commit a complete source Fragment's result after its measured length has
 * passed the common invariant. Both target code and typed common data/fills
 * take this path. Explicit .reloc and generated DWARF remain section-owned. */
/* Every source fragment, including typed MCData and target instruction,
 * commits through the same length/fixup transaction. No Target code runs
 * during this phase. Section-owned .reloc and DWARF retain their own APIs.
 *
 * On allocation failure assembly aborts without writing an output object;
 * semantic state never becomes visible to a subsequent Target invocation.
 */
static bool minias_commit_encode_result(MiniAs *as, const MiniAsStmt *owner) {
    MiniAsEncodeResult *result = as->active_encode_result;
    size_t i,logical_size=0U;
    if (result == NULL || !as->emitting_fragment ||
        owner != &as->stmts[as->emitting_fragment_id]) {
        minias_set_error(as, "encode-commit-without-fragment");
        return false;
    }
    if (result->byte_count != owner->size ||
        owner->offset > UINT64_MAX - owner->size ||
        owner->section < 0 || (size_t)owner->section >= as->section_count) {
        minias_set_error(as, "invalid-complete-encode-result:line=%zu",
                         owner->line);
        return false;
    }
    for(i=0U;i<result->write_count;++i) {
        const MiniAsFragmentWrite *write=&result->writes[i];
        if(write->size>owner->size-logical_size ||
           (write->kind==MINIAS_WRITE_COPIED_BYTES &&
            (write->copied_offset>result->byte_storage_size ||
             write->size>result->byte_storage_size-write->copied_offset)) ||
           (write->kind==MINIAS_WRITE_BORROWED_BYTES &&
            write->borrowed_bytes==NULL) ||
           (write->kind==MINIAS_WRITE_PATTERN &&
            (write->repeat_width==0U || write->repeat_width>8U ||
             write->repeat_count!=write->size/write->repeat_width ||
             write->size%write->repeat_width!=0U)) ||
           write->kind>MINIAS_WRITE_PATTERN) {
            minias_set_error(as,"invalid-fragment-write:line=%zu",owner->line);
            return false;
        }
        logical_size+=write->size;
    }
    if(logical_size!=result->byte_count) {
        minias_set_error(as,"incomplete-fragment-writes:line=%zu",owner->line);
        return false;
    }
    /* Validate requests before the first global mutation. */
    if (result->anchor_count > SIZE_MAX - as->synthetic_anchor_counter ||
        result->fixup_count > SIZE_MAX - as->fixup_count) {
        minias_set_error(as, "encoded-request-count-overflow");
        return false;
    }
    for (i = 0U; i < result->anchor_count; ++i) {
        const MiniAsEncodeAnchorRequest *request = &result->anchors[i];
        const char *name=request->name_id < as->symbol_name_count
            ? as->symbol_names[request->name_id].name : NULL;
        if (name==NULL || request->section != owner->section ||
            request->subsection != owner->subsection ||
            request->offset < owner->offset ||
            request->offset - owner->offset > owner->size ||
            minias_get_symbol(as,name,false) != NULL) {
            minias_set_error(as, "invalid-encoded-anchor:line=%zu",
                             owner->line);
            return false;
        }
    }
    for (i = 0U; i < result->fixup_count; ++i) {
        const MiniAsEncodeFixupRequest *request = &result->fixups[i];
        if (request->section != owner->section ||
            request->fragment_offset > owner->size ||
            request->width > owner->size - request->fragment_offset ||
            (!request->absolute_zero_symbol && (request->has_symbol_index ?
             request->symbol_index>=as->symbol_count :
             request->symbol_ref.name_id>=as->symbol_name_count ||
             as->symbol_names[request->symbol_ref.name_id].name==NULL))) {
            minias_set_error(as, "invalid-encoded-fixup-commit:line=%zu",
                             owner->line);
            return false;
        }
    }
    /* Reserve the Fixup backing storage before defining synthetic symbols.
     * Their final indices and relocation bindings still belong to Common. */
    if (!minias_grow_array((void **)&as->fixups, &as->fixup_capacity,
                           sizeof(*as->fixups),
                           as->fixup_count + result->fixup_count)) {
        minias_set_error(as, "out-of-memory:encoded-fixup-commit");
        return false;
    }
    for (i = 0U; i < result->anchor_count; ++i) {
        const MiniAsEncodeAnchorRequest *request = &result->anchors[i];
        const char *name=as->symbol_names[request->name_id].name;
        MiniAsSymbol *anchor = minias_get_symbol(as, name, true);
        if (anchor == NULL) return false;
        anchor->defined = true;
        anchor->section = request->section;
        anchor->subsection = request->subsection;
        anchor->value = request->offset;
        anchor->bind = MINIAS_STB_LOCAL;
    }
    for (i = 0U; i < result->fixup_count; ++i) {
        const MiniAsEncodeFixupRequest *request = &result->fixups[i];
        const char *name=request->absolute_zero_symbol || request->has_symbol_index ? NULL :
            as->symbol_names[request->symbol_ref.name_id].name;
        if (!minias_record_fixup(as, request->section,
                                 owner->offset + request->fragment_offset,
                                 request->kind, request->width,
                                 name,
                                 request->has_symbol_index ?
                                     request->symbol_index : SIZE_MAX,
                                 request->absolute_zero_symbol ? 0 : request->symbol_ref.addend,
                                 MINIAS_FIXUP_ORIGIN_FRAGMENT,
                                 request->absolute_zero_symbol))
            return false;
        as->fixups[as->fixup_count - 1U].prefer_section_symbol =
            request->prefer_section_symbol;
    }
    /* One ordered commit protocol handles any mixture of fixed DATA fields,
     * deferred relocations, borrowed source bytes and compact repeated fills.
     * There is no alternate direct-append path for a source Fragment. */
    as->committing_fragment=true;
    for(i=0U;i<result->write_count;++i) {
        const MiniAsFragmentWrite *write=&result->writes[i];
        bool materialized;
        if(write->kind==MINIAS_WRITE_PATTERN && write->repeat_width==1U)
            materialized=minias_section_append_fill(
                as,owner->section,write->repeat_count,write->pattern[0],
                write->ignore_nobits_fill);
        else if(write->kind==MINIAS_WRITE_PATTERN)
            materialized=emit_common_pattern(
                as,owner->section,write->pattern,write->repeat_width,
                write->repeat_count);
        else
            materialized=minias_section_append(
                as,owner->section,
                write->kind==MINIAS_WRITE_BORROWED_BYTES ?
                    write->borrowed_bytes : result->bytes+write->copied_offset,
                write->size);
        if(!materialized) {
            as->committing_fragment=false;
            return false;
        }
    }
    as->committing_fragment=false;
    as->synthetic_anchor_counter += result->anchor_count;
    as->elf_flags |= result->elf_flags_or;
    minias_encode_result_destroy(result);
    return true;
}

bool minias_emit_instruction_bytes(MiniAs *as, int section_index,
                                   const void *data, size_t size) {
    MiniAsSection *sec;
    MiniAsEncodeResult *result = as->active_encode_result;
    if (section_index < 0 || (size_t)section_index >= as->section_count) {
        minias_set_error(as, "invalid-target-emission-section");
        return false;
    }
    sec = &as->sections[(size_t)section_index];
    if (result == NULL || !as->emitting_fragment) {
        minias_set_error(as, "target-emission-outside-instruction");
        return false;
    }
    const MiniAsStmt *owner;
    if (!as->emitting_fragment ||
        as->emitting_fragment_id >= as->stmt_count) {
        minias_set_error(as, "instruction-bytes-without-fragment");
        return false;
    }
    owner = &as->stmts[as->emitting_fragment_id];
    /* Instruction and executable-padding fragments use the same private
     * byte transaction.  A Target cannot append alignment NOPs directly to
     * the final Section, where a failed encoder would leave partial bytes. */
    if ((owner->fragment_kind != MINIAS_FRAGMENT_INSTRUCTION &&
         !(owner->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT &&
           (sec->flags & MINIAS_SHF_EXECINSTR) != 0U &&
           !owner->align_has_fill)) ||
        owner->section != section_index ||
        sec->type == MINIAS_SHT_NOBITS ||
        size > SIZE_MAX - sec->size) {
        minias_set_error(as, "instruction-emission-section-or-size");
        return false;
    }
    if(!minias_stage_fragment_bytes(as,section_index,data,size,true))return false;
    /* The CodeEmitter owns bytes until the transaction commits. A Target
     * never updates a final Section size or cursor while encoding. */
    return true;

}

/* Source-order and final position are Common-owned properties, not private
 * target parser data.  The x86 padding policy queries this site predicate
 * without reaching through MiniAs into neighboring retained statements. */
bool minias_code_alignment_preceded_by_adjacent_data(const MiniAs *as,
                                                      int section) {
    const MiniAsStmt *owner, *previous;
    if(as==NULL || !as->emitting_fragment ||
       as->emitting_fragment_id==0U ||
       as->emitting_fragment_id>=as->stmt_count)return false;
    owner=&as->stmts[as->emitting_fragment_id];
    previous=&as->stmts[as->emitting_fragment_id-1U];
    return owner->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
           owner->section==section &&
           (previous->fragment_kind==MINIAS_FRAGMENT_FIXED_DATA ||
            previous->fragment_kind==MINIAS_FRAGMENT_FILL ||
            previous->fragment_kind==MINIAS_FRAGMENT_SPACE ||
            previous->fragment_kind==MINIAS_FRAGMENT_BYTES) &&
           previous->section==section &&
           previous->subsection==owner->subsection &&
           previous->offset<=UINT64_MAX-previous->size &&
           previous->offset+previous->size==owner->offset;
}

/* This is the one storage-capacity owner for materialized Section data.
 * Fragment emitters may append a byte sequence, a large repeated fill, or
 * a fixed-width pattern; they must not re-enter the growth policy once per
 * source element.  NOBITS is handled by each caller's semantic contract. */
static bool minias_section_reserve(MiniAs *as,MiniAsSection *sec,size_t need) {
    size_t next;
    unsigned char *p;
    if(need<=sec->capacity)return true;
    next=sec->capacity==0U?64U:sec->capacity;
    while(next<need) {
        if(next>SIZE_MAX/2U) {
            minias_set_error(as,"section-capacity-overflow:%s",sec->name);
            return false;
        }
        next*=2U;
    }
    p=realloc(sec->data,next);
    if(p==NULL) {
        minias_set_error(as,"out-of-memory:section-data");
        return false;
    }
    sec->data=p;
    sec->capacity=next;
    return true;
}

/* Staging is the only byte-producing path inside a source Fragment.
 * Raw data stays in the owned result; huge immutable byte snapshots are
 * borrowed from the retained MCStmt; fills/patterns use constant storage.
 * Section-owned CFI/DWARF calls, made after all Fragments, continue using
 * the direct materializer below. */
static bool minias_stage_fragment_bytes(MiniAs *as,int section,
                                         const void *data,size_t size,
                                         bool target_bytes) {
    MiniAsEncodeResult *result=as->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    const unsigned char *bytes=(const unsigned char *)data;
    MiniAsFragmentWrite *write;
    size_t i,needed;
    bool borrow;
    if(result==NULL || as->emitting_fragment_id>=as->stmt_count ||
       (size!=0U && data==NULL)) {
        minias_set_error(as,"invalid-fragment-bytes");
        return false;
    }
    owner=&as->stmts[as->emitting_fragment_id];
    if(owner->section!=section ||
       (target_bytes ?
        (owner->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION &&
         !(owner->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
           (sec->flags&MINIAS_SHF_EXECINSTR)!=0U &&
           !owner->align_has_fill)) :
        (owner->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
         (owner->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
          (sec->flags&MINIAS_SHF_EXECINSTR)!=0U &&
          !owner->align_has_fill))) ||
       result->byte_count>owner->size ||
       size>owner->size-result->byte_count) {
        minias_set_error(as,"fragment-byte-stage-conflict:line=%zu",owner->line);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS)
        for(i=0U;i<size;++i)
            if(bytes[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
    if(size==0U)return true;
    borrow=!target_bytes && owner->fragment_kind==MINIAS_FRAGMENT_BYTES &&
           data==owner->static_data && size==owner->static_data_size;
    if(!borrow) {
        if(size>SIZE_MAX-result->byte_storage_size) {
            minias_set_error(as,"fragment-copied-size-overflow");
            return false;
        }
        needed=result->byte_storage_size+size;
        if(!minias_grow_array((void **)&result->bytes,
                             &result->byte_capacity,sizeof(unsigned char),
                             needed)) {
            minias_set_error(as,"out-of-memory:fragment-bytes");
            return false;
        }
        memcpy(result->bytes+result->byte_storage_size,bytes,size);
    }
    if(!minias_grow_array((void **)&result->writes,
                          &result->write_capacity,sizeof(*result->writes),
                          result->write_count+1U)) {
        minias_set_error(as,"out-of-memory:fragment-writes");
        return false;
    }
    write=&result->writes[result->write_count++];
    memset(write,0,sizeof(*write));
    write->kind=borrow ? MINIAS_WRITE_BORROWED_BYTES : MINIAS_WRITE_COPIED_BYTES;
    write->size=size;
    if(borrow)write->borrowed_bytes=bytes;
    else {
        write->copied_offset=result->byte_storage_size;
        result->byte_storage_size+=size;
    }
    result->byte_count+=size;
    return true;
}

static bool minias_stage_fragment_pattern(MiniAs *as,int section,
                                           const unsigned char *pattern,
                                           size_t width,size_t count,
                                           bool ignore_nobits_fill) {
    MiniAsEncodeResult *result=as->active_encode_result;
    const MiniAsStmt *owner;
    MiniAsSection *sec=&as->sections[(size_t)section];
    MiniAsFragmentWrite *write;
    size_t size,i;
    if(result==NULL || as->emitting_fragment_id>=as->stmt_count ||
       width==0U || width>8U || count>SIZE_MAX/width) {
        minias_set_error(as,"invalid-fragment-pattern");
        return false;
    }
    owner=&as->stmts[as->emitting_fragment_id];
    size=width*count;
    if(owner->section!=section ||
       owner->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
       (owner->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
        (sec->flags&MINIAS_SHF_EXECINSTR)!=0U &&
        !owner->align_has_fill) ||
       result->byte_count>owner->size || size>owner->size-result->byte_count) {
        minias_set_error(as,"fragment-pattern-stage-conflict:line=%zu",
                         owner->line);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS && !ignore_nobits_fill)
        for(i=0U;i<width;++i)
            if(pattern[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
    if(size==0U)return true;
    if(!minias_grow_array((void **)&result->writes,
                          &result->write_capacity,sizeof(*result->writes),
                          result->write_count+1U)) {
        minias_set_error(as,"out-of-memory:fragment-writes");
        return false;
    }
    write=&result->writes[result->write_count++];
    memset(write,0,sizeof(*write));
    write->kind=MINIAS_WRITE_PATTERN;
    write->size=size;
    memcpy(write->pattern,pattern,width);
    write->repeat_width=(unsigned)width;
    write->repeat_count=count;
    write->ignore_nobits_fill=ignore_nobits_fill;
    result->byte_count+=size;
    return true;
}

bool minias_section_append(MiniAs *as, int section_index, const void *data, size_t size) {
    MiniAsSection *sec;
    size_t need;
    if (section_index < 0 || (size_t)section_index >= as->section_count) {
        minias_set_error(as, "internal:bad-section");
        return false;
    }
    sec = &as->sections[(size_t)section_index];
    if(as->emitting_fragment && !as->committing_fragment)
        return minias_stage_fragment_bytes(as,section_index,data,size,false);
    if (sec->type == MINIAS_SHT_NOBITS) {
        const unsigned char *bytes=(const unsigned char *)data;
        size_t i;
        if(size>SIZE_MAX-sec->size) {
            minias_set_error(as,"section-size-overflow:%s",sec->name);
            return false;
        }
        /* GNU rejects real nonzero data in SHT_NOBITS. Previously this
         * branch silently threw it away and produced a success ELF. */
        for(i=0U;i<size;++i) {
            if(bytes[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
        }
        sec->size+=size;
        return true;
    }
    if (size > SIZE_MAX - sec->size) {
        minias_set_error(as, "section-size-overflow:%s", sec->name);
        return false;
    }
    need = sec->size + size;
    if(!minias_section_reserve(as,sec,need))return false;
    if (size != 0U) {
        memcpy(sec->data + sec->size, data, size);
    }
    sec->size = need;
    return true;
}

/* The same append-size and allocation invariants apply to zero and nonzero
 * fills.  GNU .space/.skip ignores the nonzero fill in SHT_NOBITS, whereas
 * a literal DATA/.fill element must not quietly discard nonzero bytes. */
static bool minias_section_append_fill(MiniAs *as,int section_index,
                                        size_t size,uint8_t fill,
                                        bool ignore_nobits_fill) {
    MiniAsSection *sec;
    size_t need;
    if(section_index<0 || (size_t)section_index>=as->section_count) {
        minias_set_error(as,"internal:bad-section");return false;
    }
    sec=&as->sections[(size_t)section_index];
    if(as->emitting_fragment && !as->committing_fragment)
        return minias_stage_fragment_pattern(
            as,section_index,&fill,1U,size,ignore_nobits_fill);
    if(size>SIZE_MAX-sec->size) {
        minias_set_error(as,"section-size-overflow:%s",sec->name);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS) {
        if(fill!=0U && !ignore_nobits_fill) {
            minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
            return false;
        }
        sec->size+=size;
        return true;
    }
    need=sec->size+size;
    if(!minias_section_reserve(as,sec,need))return false;
    if(size!=0U)memset(sec->data+sec->size,(int)fill,size);
    sec->size=need;
    return true;
}

bool minias_section_append_zero(MiniAs *as,int section_index,size_t size) {
    return minias_section_append_fill(as,section_index,size,0U,false);
}

/* Context construction is one fail-closed transaction. Earlier code
 * ignored section/subsection allocations, then invoked Target initialization
 * against a partial Common object. A failed init is destroyed by the caller
 * without parsing or emitting an output ELF. */
static bool minias_init_for_target(MiniAs *as,
                                   const MiniAsTargetOps *target) {
    int text;
    memset(as,0,sizeof(*as));
    as->target=target;
    if(target==NULL) {
        minias_set_error(as,"missing-assembler-target");
        return false;
    }
    text=minias_ensure_section(as,".text",MINIAS_SHT_PROGBITS,
            MINIAS_SHF_ALLOC|MINIAS_SHF_EXECINSTR,
            target->default_exec_alignment!=0U ?
            target->default_exec_alignment:4U);
    if(text<0 || minias_ensure_section(as,".data",MINIAS_SHT_PROGBITS,
                     MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U)<0 ||
       minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                     MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U)<0)
        return false;
    as->current_section=text;
    as->current_subsection=0U;
    as->previous_section=text;
    as->previous_subsection=0U;
    as->conditional_active=true;
    as->debug_is_stmt=true;
    return minias_ensure_subsection(as,text,0U)!=NULL;
}

static uint64_t minias_target_exec_alignment(const MiniAs *as) {
    unsigned align=as->target->default_exec_alignment_for_site!=NULL ?
        as->target->default_exec_alignment_for_site(as) : 0U;
    if(align==0U)align=as->target->default_exec_alignment;
    return align!=0U ? align : 4U;
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
    free(macro->body_source_segments);
    memset(macro,0,sizeof(*macro));
}

void minias_destroy(MiniAs *as) {
    size_t i;
    minias_layout_destroy(as);

    for (i = 0U; i < as->section_count; ++i) {
        free(as->sections[i].name);
        free(as->sections[i].group_signature);
        free(as->sections[i].link_order_reference);
        free(as->sections[i].data);
    }
    free(as->sections);
    free(as->section_slots);
    if(as->target && as->target->destroy_target_state)
        as->target->destroy_target_state(as);
    for (i = 0U; i < as->symbol_count; ++i) {
        free(as->symbols[i].name);
        free(as->symbols[i].alias_section_base);
    }
    free(as->symbols);
    free(as->symbol_slots);
    for (i = 0U; i < as->symbol_name_count; ++i)
        free(as->symbol_names[i].name);
    free(as->symbol_names);
    free(as->symbol_name_slots);
    for (i = 0U; i < as->stmt_count; ++i) {
        free(as->stmts[i].op);
        free(as->stmts[i].args);
        minias_deferred_expr_destroy(as->stmts[i].org_expr);
        free(as->stmts[i].static_data);
        minias_mcdata_destroy(as->stmts[i].mcdata);
        for (size_t k=0U;k<3U;++k)
            minias_deferred_expr_destroy(as->stmts[i].variable_exprs[k]);
        minias_mcinst_destroy(as->stmts[i].mcinst);
    }
    free(as->stmts);
    free(as->fixups);
    if (as->active_encode_result != NULL) {
        minias_encode_result_destroy(as->active_encode_result);
        free(as->active_encode_result);
    }
    free(as->literals);
    free(as->subsections);
    free(as->subsection_slots);
    free(as->section_stack);
    free(as->numeric_label_counts);
    free(as->conditionals);
    for(i=0U;i<as->macro_count;++i)
        release_macro(&as->macros[i]);
    free(as->macros);
    free(as->cfi_procs);
    for(i=0U;i<as->cfi_op_count;++i)
        free(as->cfi_ops[i].bytes);
    free(as->cfi_ops);
    for (i = 0U; i < as->debug_file_capacity; ++i)
        free(as->debug_files[i]);
    free(as->debug_files);
    for (i = 0U; i < as->cu_file_count; ++i)
        free(as->cu_files[i]);
    free(as->cu_files);
    free(as->debug_locs);
    memset(as, 0, sizeof(*as));
}

static bool add_stmt(MiniAs *as,
                     MiniAsFragmentKind fragment_kind,
                     const char *op,
                     const char *args,
                     size_t line,
                     uint32_t size,
                     uint64_t align) {
    MiniAsStmt *st;

    if (!minias_grow_array((void **)&as->stmts,
                    &as->stmt_capacity,
                    sizeof(*as->stmts),
                    as->stmt_count + 1U)) {
        minias_set_error(as, "out-of-memory:stmt");
        return false;
    }
    st = &as->stmts[as->stmt_count];
    memset(st, 0, sizeof(*st));
    st->fragment_kind = fragment_kind;
    st->source_site = as->current_source_site;
    st->op = minias_strdup(op);
    st->args = minias_strdup(args == NULL ? "" : args);
    st->line = line;
    st->section = as->current_section;
    st->subsection = as->current_subsection;
    if (!minias_current_location(as, &st->offset)) {
        free(st->op);
        free(st->args);
        st->op = NULL;
        st->args = NULL;
        return false;
    }
    st->size = size;
    st->align = align;
    if (as->target->capture_site_state != NULL)
        as->target->capture_site_state(as, &st->site);
    if (st->op == NULL || st->args == NULL) {
        minias_set_error(as, "out-of-memory:stmt-text");
        return false;
    }
    ++as->stmt_count;
    if (fragment_kind == MINIAS_FRAGMENT_INSTRUCTION &&
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

bool minias_parse_u64(const char *text, uint64_t *out) {
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

static bool switch_section(MiniAs *as,
                           const char *name,
                           const char *group_signature,
                           const char *link_order_reference,
                           uint32_t type,
                           uint64_t flags,
                           uint64_t align) {
    int index = minias_ensure_section_key(as, name, group_signature,
                                          link_order_reference,
                                          type, flags, align);

    if (index < 0) {
        return false;
    }
    if (index != as->current_section || as->current_subsection != 0U) {
        as->previous_section = as->current_section;
        as->previous_subsection = as->current_subsection;
        as->current_section = index;
        as->current_subsection = 0U;
    }
    return minias_ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
}

/* GAS's implicit section names are family members only at the exact
 * name or a dot boundary: .bss.hot is NOBITS, .bssification is not.
 * .note.GNU-stack is intentionally PROGBITS without SHF_ALLOC. */
static bool gnu_section_family(const char *name,const char *root) {
    size_t n=strlen(root);
    return strncmp(name,root,n)==0 && (name[n]=='\0' || name[n]=='.');
}

static bool parse_section_directive(MiniAs *as, char *args, size_t line) {
    char *comma = NULL,*scan=args;
    char *name;
    bool quoted=false,escaped=false;
    uint64_t flags = 0U;
    uint32_t type = MINIAS_SHT_PROGBITS;
    uint64_t entry_size=0U;
    const char *group_signature=NULL;
    const char *link_order_reference=NULL;

    /* A quoted GNU section name may contain literal commas. Only a
     * top-level comma separates its name from flags/type metadata. */
    for(;*scan;++scan) {
        if(quoted && *scan=='\\' && !escaped) {escaped=true;continue;}
        if(*scan=='"' && !escaped)quoted=!quoted;
        if(*scan==',' && !quoted) {comma=scan;break;}
        escaped=false;
    }
    if(quoted) {
        minias_set_error(as,"unterminated-section-name:line=%zu",line);
        return false;
    }
    if (comma != NULL) {
        *comma = '\0';
    }
    name = minias_trim(args);
    if (*name == '"') {
        size_t n = strlen(name);
        if(n<2U || name[n-1U]!='"') {
            minias_set_error(as,"bad-quoted-section-name:line=%zu",line);
            return false;
        }
        name[n-1U]='\0';++name;
    }
    if (*name == '\0') {
        minias_set_error(as, "bad-directive:.section:line=%zu", line);
        return false;
    }

    if (gnu_section_family(name,".bss") ||
        gnu_section_family(name,".tbss")) {
        type=MINIAS_SHT_NOBITS;
    } else if(gnu_section_family(name,".note") &&
              strcmp(name,".note.GNU-stack")!=0) {
        type=MINIAS_SHT_NOTE;
    } else if(gnu_section_family(name,".init_array")) {
        type=MINIAS_SHT_INIT_ARRAY;
    } else if(gnu_section_family(name,".fini_array")) {
        type=MINIAS_SHT_FINI_ARRAY;
    } else if(gnu_section_family(name,".preinit_array")) {
        type=MINIAS_SHT_PREINIT_ARRAY;
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
                } else if (*q == 'M') {
                    flags |= MINIAS_SHF_MERGE;
                } else if (*q == 'S') {
                    flags |= MINIAS_SHF_STRINGS;
                } else if (*q == 'T') {
                    flags |= MINIAS_SHF_TLS;
                } else if (*q == 'G') {
                    flags |= MINIAS_SHF_GROUP;
                } else if (*q == 'R') {
                    flags |= MINIAS_SHF_GNU_RETAIN;
                } else if (*q == 'o') {
                    flags |= MINIAS_SHF_LINK_ORDER;
                } else {
                    /* A silently dropped flag can change ELF selection,
                     * merging, relocation or linker garbage collection.
                     * GNU's unknown letters are fatal, not advisory. */
                    minias_set_error(as,"unsupported-section-flag:%c:%s:line=%zu",
                                     *q,name,line);
                    return false;
                }
                ++q;
            }
            if (*q != '"') {
                minias_set_error(as,"unterminated-section-flags:%s:line=%zu",
                                 name,line);
                return false;
            }
            flag_end = q;
            type_separator = strchr(flag_end, ',');
        } else {
            type_separator = strchr(rest, ',');
        }

        if (type_separator != NULL) {
            char *kind = minias_trim(type_separator + 1);
            char *fields[5];
            size_t field_count=0U,next=0U;
            char *cursor=kind,*tail;
            while((tail=strchr(cursor,','))!=NULL) {
                if(field_count>=sizeof(fields)/sizeof(fields[0])) {
                    minias_set_error(as,"too-many-section-parameters:%s:line=%zu",name,line);
                    return false;
                }
                *tail='\0';
                fields[field_count++]=minias_trim(tail+1);
                cursor=tail+1;
            }
            /* Extra section arguments are consumed in ELF syntax order:
             * M entsize, o linked-to symbol, G signature and linkage. */
            if((flags & MINIAS_SHF_MERGE)!=0U) {
                if(next>=field_count ||
                   !minias_parse_u64(fields[next++],&entry_size) ||
                   entry_size==0U) {
                    minias_set_error(as,"bad-merge-entry-size:%s:line=%zu",name,line);
                    return false;
                }
            }
            if((flags & MINIAS_SHF_LINK_ORDER)!=0U) {
                /* A missing anchor is accepted by GAS as sh_link=0.  An
                 * explicit empty argument is not a valid symbol.  Store a
                 * unique empty identity so this never aliases a plain
                 * same-named Section without SHF_LINK_ORDER. */
                link_order_reference="";
                if(next<field_count) {
                    const char *linked=fields[next++];
                    if(*linked=='\0' || strlen(linked)>=256U ||
                       strpbrk(linked," \t,\r\n\"")!=NULL) {
                        minias_set_error(as,"bad-link-order-reference:%s:line=%zu",name,line);
                        return false;
                    }
                    link_order_reference=linked;
                }
            }
            if((flags & MINIAS_SHF_GROUP)!=0U) {
                const char *sig,*mode;
                if(field_count-next<2U) {
                    minias_set_error(as,"missing-group-comdat:%s:line=%zu",name,line);
                    return false;
                }
                sig=fields[next++];mode=fields[next++];
                if(*sig=='\0' || strcmp(mode,"comdat")!=0 ||
                   strlen(sig)>=256U ||
                   strpbrk(sig," \t,\r\n\"")!=NULL) {
                    minias_set_error(as,"bad-section-comdat:%s:line=%zu",name,line);
                    return false;
                }
                group_signature=sig;
            }
            if(next!=field_count) {
                minias_set_error(as,"unexpected-section-parameter:%s:line=%zu",name,line);
                return false;
            }
            if (*kind == '@' || *kind == '%') {
                ++kind;
            }
            if (strcmp(kind, "note") == 0) {
                type = MINIAS_SHT_NOTE;
            } else if (strcmp(kind, "nobits") == 0) {
                type = MINIAS_SHT_NOBITS;
            } else if (strcmp(kind, "progbits") == 0) {
                type = MINIAS_SHT_PROGBITS;
            } else if (strcmp(kind, "init_array") == 0) {
                type = MINIAS_SHT_INIT_ARRAY;
            } else if (strcmp(kind, "fini_array") == 0) {
                type = MINIAS_SHT_FINI_ARRAY;
            } else if (strcmp(kind, "preinit_array") == 0) {
                type = MINIAS_SHT_PREINIT_ARRAY;
            } else {
                minias_set_error(as,"unsupported-section-type:%s:line=%zu",name,line);
                return false;
            }
        } else if((flags & MINIAS_SHF_LINK_ORDER)!=0U ||
                  (flags & MINIAS_SHF_GROUP)!=0U ||
                  (flags & MINIAS_SHF_MERGE)!=0U) {
            minias_set_error(as,"missing-section-type:%s:line=%zu",name,line);
            return false;
        }
    }

    if ((flags & MINIAS_SHF_GROUP)!=0U &&
        ((flags & MINIAS_SHF_MERGE)!=0U || group_signature==NULL)) {
        minias_set_error(as,"unsupported-comdat-flags:%s:line=%zu",name,line);
        return false;
    }
    if (flags == 0U) {
        if(gnu_section_family(name,".text")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR;
        } else if(gnu_section_family(name,".rodata")) {
            flags=MINIAS_SHF_ALLOC;
        } else if(gnu_section_family(name,".data") ||
                  gnu_section_family(name,".bss")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        } else if(gnu_section_family(name,".tdata") ||
                  gnu_section_family(name,".tbss")) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE | MINIAS_SHF_TLS;
        } else if(type==MINIAS_SHT_INIT_ARRAY ||
                  type==MINIAS_SHT_FINI_ARRAY ||
                  type==MINIAS_SHT_PREINIT_ARRAY) {
            flags=MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE;
        }
        /* GNU note sections are NOT implicitly SHF_ALLOC; only an
         * explicit 'a' allocates .note.* payloads. */
    }
    if(entry_size==0U &&
       (type==MINIAS_SHT_INIT_ARRAY || type==MINIAS_SHT_FINI_ARRAY ||
        type==MINIAS_SHT_PREINIT_ARRAY))
        entry_size=as->output_elf32?4U:8U;
    if ((flags & MINIAS_SHF_MERGE)!=0U && entry_size==0U) {
        minias_set_error(as,"missing-merge-entry-size:%s:line=%zu",name,line);
        return false;
    }
    /*
     * RISC-V instructions in executable input subsections must not be
     * placed at byte alignment.  In particular .text.main may follow
     * odd-length .rodata when MiniLD links section-per-function inputs.
     * The .text directive already uses four-byte instruction alignment.
     */
    {
        int prior=minias_find_section_key(as,name,group_signature,
                                          link_order_reference);
        bool ok=switch_section(as,name,group_signature,link_order_reference,type,flags,
                (flags & MINIAS_SHF_EXECINSTR)!=0U ?
                   minias_target_exec_alignment(as):1U);
        /* sh_entsize is a property of the *first* ELF section creation.
         * Later incompatible .section definitions are ignored by GAS. */
        if(ok && prior<0 && entry_size!=0U)
            as->sections[(size_t)as->current_section].entry_size=entry_size;
        if (ok) {
            if (group_signature!=NULL && prior<0) {
                MiniAsSymbol *signature;
                signature=minias_get_symbol(as,group_signature,true);
                if(signature==NULL)return false;
                /* GAS synthesizes an undefined default-local group
                 * signature as a LOCAL symbol in the new SHT_GROUP section.
                 * An explicitly .globl signature remains GLOBAL UND. */
            }
        }
        return ok;
    }
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
    } else if(strcmp(kind,"@notype")==0 || strcmp(kind,"%notype")==0 ||
              strcmp(kind,"STT_NOTYPE")==0) {
        symbol->type=MINIAS_STT_NOTYPE;
    } else if(strcmp(kind,"@tls_object")==0 || strcmp(kind,"%tls_object")==0 ||
              strcmp(kind,"STT_TLS")==0) {
        symbol->type=MINIAS_STT_TLS;
    } else if(strcmp(kind,"@common")==0 || strcmp(kind,"%common")==0 ||
              strcmp(kind,"STT_COMMON")==0) {
        /* GAS treats @common as STT_OBJECT + GLOBAL binding, not as
         * an ELF STT_COMMON (5) symbol. This is observable in symtab. */
        symbol->type=MINIAS_STT_OBJECT;
        symbol->bind=MINIAS_STB_GLOBAL;
    } else if(strcmp(kind,"@gnu_unique_object")==0 ||
              strcmp(kind,"%gnu_unique_object")==0) {
        symbol->type=MINIAS_STT_OBJECT;
        symbol->bind=MINIAS_STB_GNU_UNIQUE;
    } else if(strcmp(kind,"@gnu_indirect_function")==0 ||
              strcmp(kind,"%gnu_indirect_function")==0 ||
              strcmp(kind,"STT_GNU_IFUNC")==0) {
        symbol->type=MINIAS_STT_GNU_IFUNC;
    } else {
        minias_set_error(as,"unsupported-elf-symbol-type:%s:line=%zu",
                         kind,line);
        return false;
    }
    return true;
}

static bool parse_size(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    MiniAsSymbol *symbol;
    MiniAsSymbol *start_symbol;
    char *expr;
    int64_t size_addend;
    size_t start_symbol_index;

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
        if (minias_parse_u64(expr, &explicit_size)) {
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
    start_symbol=symbol;
    start_symbol_index=(size_t)(symbol-as->symbols);
    if (!minias_parse_size_end_addend(expr,symbol->name,&size_addend)) {
        MiniAsSymbolDifference difference;
        if(!minias_parse_symbol_difference_absolute_tail(as,expr,&difference) ||
           strcmp(difference.positive.name,".")!=0 ||
           (start_symbol=minias_get_symbol(as,difference.negative.name,false))==NULL ||
           !start_symbol->defined || !start_symbol->has_fragment_anchor ||
           !minias_parse_size_end_addend(expr,start_symbol->name,&size_addend)) {
            minias_set_error(as,"unsupported-expression:.size:%s:line=%zu",expr,line);
            return false;
        }
        start_symbol_index=(size_t)(start_symbol-as->symbols);
    }
    if (symbol->section != as->current_section ||
        start_symbol->section != as->current_section) {
        minias_set_error(as, "size-section-mismatch:%s:line=%zu", symbol->name, line);
        return false;
    }
    symbol->has_size_start_symbol=start_symbol!=symbol;
    symbol->size_start_symbol_index=start_symbol_index;
    if(symbol->subsection!=as->current_subsection ||
       start_symbol->subsection!=as->current_subsection) {
        /* GAS joins numbered subsections before evaluating symbol sizes.
         * A span from subsection 0 to subsection 1 cannot be computed from
         * parse-site LOCAL offsets: capture its end owner and let MCLayout
         * subtract the two final Section-relative Fragment anchors. */
        symbol->has_size_end_anchor=true;
        symbol->size_end_anchor=as->stmt_count;
        symbol->size_end_subsection=as->current_subsection;
        symbol->size_end_addend=size_addend;
        symbol->size=0U;
        as->has_cross_subsection_size_end=true;
        return true;
    }
    {
        uint64_t here;
        int64_t end;
        if (!minias_current_location(as, &here) ||
            here>(uint64_t)INT64_MAX ||
            !minias_checked_add_i64((int64_t)here,size_addend,&end) ||
            end<0 || (uint64_t)end < start_symbol->value) {
            minias_set_error(as, "size-location-mismatch:%s:line=%zu", symbol->name, line);
            return false;
        }
        symbol->size = (uint64_t)end - start_symbol->value;
        symbol->has_size_end_anchor = true;
        symbol->size_end_anchor = as->stmt_count;
        symbol->size_end_subsection = as->current_subsection;
        symbol->size_end_addend = size_addend;
    }
    return true;
}

/* A symbol alias carries a stable SymbolID edge.  The first parse resolves
 * forward references, and subsequent MCLayout passes refresh values after
 * branch/data growth.  Do not intern a second symbol under the same name. */
static bool bind_pending_alias(MiniAs *as,const char *name,size_t target_index,
                               int64_t addend,size_t line) {
    MiniAsSymbol *alias=minias_get_symbol(as,name,true);
    if(alias==NULL)return false;
    if(alias->defined || alias->alias_pending || alias->has_alias_dependency) {
        minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
        return false;
    }
    alias->has_alias_dependency=true;
    alias->alias_pending=true;
    alias->alias_target_index=target_index;
    alias->alias_addend=addend;
    alias->alias_line=line;
    return true;
}

static bool parse_equ(MiniAs *as, char *args, size_t line) {
    char *comma = strchr(args, ',');
    char *name;
    char *expression;
    int64_t value;
    MiniAsSymbol *symbol;
    MiniAsSymbolExpr expr={0};

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
    if (*name == '\0' || *expression == '\0') {
        minias_set_error(as,
                         "unsupported-expression:.equ:%s:line=%zu",
                         expression,
                         line);
        return false;
    }
    if(minias_parse_symbol_addend(expression,&expr)) {
        if(strcmp(expr.name,".")==0) {
            uint64_t here,position;
            if(!minias_current_location(as,&here) ||
               (expr.addend>=0 && (uint64_t)expr.addend>UINT64_MAX-here) ||
               (expr.addend<0 &&
                (uint64_t)(-(expr.addend+1))+1U>here)) {
                minias_set_error(as,"equ-location-overflow:line=%zu",line);
                return false;
            }
            position=expr.addend>=0 ? here+(uint64_t)expr.addend :
                here-((uint64_t)(-(expr.addend+1))+1U);
            symbol=minias_get_symbol(as,name,true);
            if(!symbol)return false;
            if(symbol->defined || symbol->alias_pending) {
                minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
                return false;
            }
            symbol->defined=true;
            symbol->section=as->current_section;
            symbol->subsection=as->current_subsection;
            symbol->value=position;
            symbol->has_fragment_anchor=true;
            symbol->fragment_anchor=as->stmt_count;
            symbol->fragment_anchor_addend=expr.addend;
            return true;
        }
        {
            MiniAsSymbol *target=minias_get_symbol(as,expr.name,true);
            size_t target_index;
            if(!target)return false;
            target_index=(size_t)(target-as->symbols);
            if(!target->defined || target->section!=MINIAS_SECTION_ABS)
                return bind_pending_alias(as,name,target_index,expr.addend,line);
        }
    }
    if(!minias_evaluate_absolute_expression(as, expression, &value)) {
        minias_set_error(as,"unsupported-expression:.equ:%s:line=%zu",
                         expression,line);
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
            if (!minias_evaluate_absolute_expression(as, expression, &value) || value < 0) {
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
            {
                MiniAsSymbolExpr location = {0};
                if (minias_parse_symbol_addend(expression, &location) &&
                    strcmp(location.name, ".") == 0) {
                    /* Capture the source-site symbol as an immutable
                     * Fragment anchor. Relaxation moves the anchor, not
                     * the already-parsed GNU assignment statement. */
                    alias->has_fragment_anchor = true;
                    alias->fragment_anchor = as->stmt_count;
                    alias->fragment_anchor_addend = location.addend;
                }
            }
            return true;
        }
    }

    /* Section-relative or forward source symbols are relocatable even if
     * the absolute evaluator can currently return a numeric offset.  Bind
     * their identity first; only known ABS .set counters are snapshotted. */
    if (minias_parse_symbol_addend(expression,&expr) &&
        strcmp(expr.name,".")!=0) {
        target=minias_get_symbol(as,expr.name,true);
        if(!target)return false;
        target_index=(size_t)(target-as->symbols);
        if(!target->defined || target->section!=MINIAS_SECTION_ABS)
            return bind_pending_alias(as,name,target_index,expr.addend,line);
    }

    /* GNU .set is mutable, unlike .equ. Evaluate an already-defined
     * absolute symbol RHS at the *current* statement position, including
     * self-referential .set counter, counter+1 in .irp/macro expansion.
     * Preserve relocatable symbol aliases via the existing fallthrough. */
    {
        int64_t value;
        if(minias_evaluate_absolute_expression(as,expression,&value)) {
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
        return bind_pending_alias(as,name,target_index,expr.addend,line);
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
                            result->has_alias_dependency=true;
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
        if (!minias_evaluate_absolute_expression(as, expression, &value) || value < 0) {
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

/* A single SymbolID +/- a constant is represented as a retained alias
 * dependency, including forward .equ/.set targets. MCLayout refreshes the
 * entire dependency chain after every Fragment growth. Reopening the GNU
 * source for this grammar would discard that stable MC symbol graph and
 * needlessly re-expand macros and conditional source around a branch.
 *
 * More general arithmetic (. - label, A - B, ...) can depend on the
 * original lexical location and still uses the conservative reparse guard.
 */
static bool is_persistent_symbol_assignment(const char *expression) {
    MiniAsSymbolExpr parsed = {0};
    int64_t literal;
    return minias_eval_i64_expression(expression, &literal) ||
           minias_parse_symbol_addend(expression, &parsed);
}

/* Parse and retain the position constraint separately from its GNU optional
 * fill operand. The fill byte belongs to the Common Fragment emitter, not
 * to the instruction target or the symbol-layout solver. */
static bool handle_org_position(MiniAs *as, const char *args,
                                const char *source_args, size_t line,
                                bool has_fill, uint8_t fill) {
    int64_t evaluated;
    int64_t lexical_target;
    MiniAsSymbolExpr symbolic = {0};
    MiniAsSymbol *symbol = NULL;
    MiniAsDeferredExpr *deferred = NULL;
    bool ready;
    uint64_t desired;
    uint64_t current;
    uint64_t pad;

    ready=minias_evaluate_absolute_expression(as,args,&evaluated);
    if(!ready) {
        /* A multi-level GNU .set/.equ alias may not have its final value
         * during lexical parsing. Retain the expression as a SymbolID AST
         * and let the two-phase MCLayout fixed point resolve it after all
         * definitions and moving subsection bases are available. Do NOT
         * treat an unknown symbol as zero or fabricate parser-time bytes. */
        deferred=minias_compile_deferred_expr(as,args);
        if(deferred==NULL)return false;
    } else if(evaluated<0) {
        minias_set_error(as, "unsupported-expression:.org:%s:line=%zu", args, line);
        return false;
    }
    desired = ready ? (uint64_t)evaluated : 0U;
    if (!minias_current_location(as, &current)) {
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    if (ready && desired < current) {
        minias_set_error(as,
                         "org-backwards:%s:current=%llu:target=%llu:line=%zu",
                         args,
                         (unsigned long long)current,
                         (unsigned long long)desired,
                         line);
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    pad = ready ? desired - current : 0U;
    if (ready && pad > UINT32_MAX) {
        minias_set_error(as, "org-gap-too-large:%s:line=%zu", args, line);
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    if (!add_stmt(as,
                  MINIAS_FRAGMENT_ORIGIN,
                  ".org",
                  source_args,
                  line,
                  (uint32_t)pad,
                  1U)) {
        minias_deferred_expr_destroy(deferred);
        return false;
    }
    as->stmts[as->stmt_count - 1U].align_has_fill = has_fill;
    as->stmts[as->stmt_count - 1U].align_fill = fill;
    if(deferred!=NULL) {
        MiniAsStmt *fragment=&as->stmts[as->stmt_count-1U];
        fragment->org_expr=deferred;
        fragment->org_mode=MINIAS_ORG_TYPED_EXPR;
        as->has_org_constraints=true;
        return true; /* Zero-size placeholder; MCLayout owns its padding. */
    }
    /* Preserve the GNU expression as a typed position constraint. A source
     * string is kept only for diagnostics or unsupported control grammar. */
    if (minias_eval_i64_expression(args, &lexical_target) &&
        lexical_target == evaluated) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_ABSOLUTE;
        fragment->org_target = desired;
        as->has_org_constraints = true;
    } else if (minias_parse_symbol_addend(args, &symbolic) &&
               strcmp(symbolic.name, ".") == 0 &&
               symbolic.addend >= 0) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_DOT_ADDEND;
        fragment->org_target = (uint64_t)symbolic.addend;
        as->has_org_constraints = true;
    } else if (minias_parse_symbol_addend(args, &symbolic) &&
               (symbol = minias_get_symbol(as, symbolic.name, false)) != NULL &&
               symbol->defined && symbol->section == MINIAS_SECTION_ABS) {
        /* Lexically resolved .set/.equ values are frozen at this site. */
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_ABSOLUTE;
        fragment->org_target = desired;
        as->has_org_constraints = true;
    } else if (symbol != NULL && symbol->defined &&
               symbol->has_fragment_anchor &&
               symbol->section == as->current_section &&
               /* Lower-numbered subsections precede this output location;
                * a later subsection would create a circular .org dependency. */
               symbol->subsection <= as->current_subsection &&
               symbol->fragment_anchor < as->stmt_count) {
        MiniAsStmt *fragment = &as->stmts[as->stmt_count - 1U];
        fragment->org_mode = MINIAS_ORG_SYMBOL_ADDEND;
        fragment->org_symbol_index = (size_t)(symbol - as->symbols);
        fragment->org_addend = symbolic.addend;
        as->has_org_constraints = true;
    } else {
        /* Retain the complete GNU expression as stable SymbolID operations.
         * Its dot and symbol values are evaluated only from MCLayout, not
         * by re-entering a source parser with mutable current-section. */
        MiniAsStmt *fragment=&as->stmts[as->stmt_count - 1U];
        fragment->org_expr=minias_compile_deferred_expr(as,args);
        if(fragment->org_expr==NULL)return false;
        fragment->org_mode=MINIAS_ORG_TYPED_EXPR;
        as->has_org_constraints=true;
    }
    return advance_current_location(as, pad);
}

static bool handle_org(MiniAs *as, const char *args, size_t line) {
    char *copy = minias_strdup(args);
    char *position, *comma;
    uint8_t fill = 0U;
    bool has_fill = false;
    bool ok;
    if(copy == NULL) {
        minias_set_error(as,"out-of-memory:org-expression");
        return false;
    }
    comma = strchr(copy, ',');
    if(comma != NULL) {
        int64_t value;
        char *fill_expr;
        *comma++ = '\0';
        fill_expr = minias_trim(comma);
        if(*fill_expr == '\0' || strchr(fill_expr, ',') != NULL ||
           !minias_evaluate_absolute_expression(as,fill_expr,&value)) {
            minias_set_error(as,"bad-directive:.org-fill:line=%zu",line);
            free(copy);
            return false;
        }
        has_fill = true;
        fill = (uint8_t)value;
    }
    position = minias_trim(copy);
    ok = handle_org_position(as,position,args,line,has_fill,fill);
    free(copy);
    return ok;
}

/* A new ISA may request a pool address without teaching Core the ISA
 * encoding or introducing a parallel ELF writer.  Records are grouped by
 * SECTION+SUBSECTION and deduped only until the next .ltorg flush. */
bool minias_queue_literal(MiniAs *as,const MiniAsSymbolExpr *expression,
                          unsigned width,MiniAsSymbolExpr *pool_label) {
    MiniAsLiteral *entry;
    MiniAsSymbolRef bound;
    size_t i;
    int written;
    if(!as->target->supports_literal_pools || as->current_section<0 ||
       !expression || !pool_label || (width!=4U && width!=8U)) {
        minias_set_error(as,"unsupported-target-literal-pool");return false;
    }
    if(!minias_bind_symbol_ref(as,expression,&bound))return false;
    for(i=0U;i<as->literal_count;++i) {
        entry=&as->literals[i];
        if(!entry->emitted && entry->section==as->current_section &&
           entry->subsection==as->current_subsection &&
           entry->width==width &&
           entry->expression.addend==bound.addend &&
           entry->expression.name_id==bound.name_id) {
            written=snprintf(pool_label->name,sizeof(pool_label->name),
                             "%s",entry->label);
            if(written<0 || (size_t)written>=sizeof(pool_label->name))
                return false;
            pool_label->addend=0;return true;
        }
    }
    if(as->literal_count==SIZE_MAX ||
       !minias_grow_array((void **)&as->literals,&as->literal_capacity,
                   sizeof(*as->literals),as->literal_count+1U)) {
        minias_set_error(as,"out-of-memory:literal-pool");return false;
    }
    entry=&as->literals[as->literal_count];
    memset(entry,0,sizeof(*entry));
    entry->section=as->current_section;
    entry->subsection=as->current_subsection;
    entry->width=width;entry->expression=bound;
    if(!minias_make_private_symbol_name(entry->label,sizeof(entry->label),
                                        "literal",as->literal_count)) {
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
       !minias_evaluate_absolute_expression(as,parts[0],&value) ||
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
        if(!minias_evaluate_absolute_expression(as,parts[1],&value) ||
           value<-128 || value>255){free(copy);return false;}
        *has_fill=true;*fill=(uint8_t)value;
    }
    if(n>=3U && *parts[2]){
        if(!minias_evaluate_absolute_expression(as,parts[2],&value) ||
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
    if(!minias_current_location(as,&current))return false;
    pad=(alignment-(current&(alignment-1U)))&(alignment-1U);
    if(has_max && pad>max_skip)pad=0U;
    if(pad>UINT32_MAX)return false;
    if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,op,"",line,(uint32_t)pad,alignment))
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

/* Shared directive grammar is the sole source of aliases that can inject
 * raw bytes into an executable section. The identical category controls
 * Common dispatch and the Target ISA-escape policy callback. This prevents
 * future .hword/.skip/.uleb128-style aliases bypassing strict RV profiles. */
static MiniAsCommonDirectiveKind common_directive_kind(const MiniAs *as,
                                                        const char *op) {
    static const char *const raw_data[] = {
        ".fill", ".zero", ".space", ".skip", ".uleb128", ".sleb128",
        ".asciz", ".string", ".ascii", ".incbin"
    };
    size_t i;
    if(!strcmp(op,".org"))return MINIAS_COMMON_DIRECTIVE_ORG;
    if(data_width(as,op)!=0U)return MINIAS_COMMON_DIRECTIVE_RAW_DATA;
    for(i=0U;i<sizeof(raw_data)/sizeof(raw_data[0]);++i)
        if(!strcmp(op,raw_data[i]))return MINIAS_COMMON_DIRECTIVE_RAW_DATA;
    return MINIAS_COMMON_DIRECTIVE_OTHER;
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
        bool ok=minias_evaluate_absolute_expression(as,lhs,&left);
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
           !minias_evaluate_absolute_expression(as,lhs,&left) ||
           !minias_evaluate_absolute_expression(as,rhs,&right)) {
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
        if(!minias_evaluate_absolute_expression(as,lhs,&left)) {
            free(copy);return false;
        }
        *result=left!=0;
    }
    free(copy);return true;
}

static bool push_conditional_value(MiniAs *as,bool condition_true) {
    MiniAsConditional *conditional;
    if (!minias_grow_array((void **)&as->conditionals,
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
    conditional->source_site = as->current_source_site;
    as->conditional_active = conditional->parent_active && condition_true;
    source_graph_record_condition(as,conditional->source_site,
                                  as->conditional_active);
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
    if(!minias_evaluate_absolute_expression(as,args,&value)) {
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
    source_graph_record_condition(as,conditional->source_site,
                                  as->conditional_active);
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
    source_graph_record_condition(as,conditional->source_site,
                                  as->conditional_active);
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

/* A source-data Fragment is immutable. Capture .incbin binary bytes
 * once, before changing the Fragment graph, instead of measuring by ftell
 * and then reopening the pathname after layout/branch relaxation.
 * Detect a concurrent source mutation during capture and fail closed. */
static bool capture_incbin(MiniAs *as,const char *args,size_t line,
                           unsigned char **data,size_t *size) {
    char *path=decode_incbin_path(as,args,line);
    FILE *file;
    long end;
    unsigned char *buffer;
    size_t count,used=0U;
    if(!path)return false;
    file=fopen(path,"rb");
    if(!file) {
        minias_set_error(as,"incbin-open:%s:line=%zu",path,line);
        free(path);
        return false;
    }
    if(fseek(file,0L,SEEK_END)!=0 || (end=ftell(file))<0L ||
       (uint64_t)end>UINT32_MAX || fseek(file,0L,SEEK_SET)!=0) {
        minias_set_error(as,"incbin-size:%s:line=%zu",path,line);
        fclose(file);free(path);return false;
    }
    count=(size_t)end;
    buffer=malloc(count?count:1U);
    if(!buffer) {
        minias_set_error(as,"out-of-memory:incbin-snapshot");
        fclose(file);free(path);return false;
    }
    while(used<count) {
        size_t got=fread(buffer+used,1U,count-used,file);
        if(got==0U) {
            minias_set_error(as,"incbin-read:%s:line=%zu",path,line);
            free(buffer);fclose(file);free(path);return false;
        }
        used+=got;
    }
    if(fgetc(file)!=EOF || ferror(file)) {
        minias_set_error(as,"incbin-mutated-during-read:%s:line=%zu",
                         path,line);
        free(buffer);fclose(file);free(path);return false;
    }
    fclose(file);
    free(path);
    *data=buffer;
    *size=count;
    return true;
}

/* GAS .skip/.space/.zero share count[,fill] semantics.
 * Parse the optional fill once through the existing numeric grammar;
 * unsupported relocatable fills remain errors rather than fake zero bytes. */
/* Shared GAS .skip/.space/.zero expression grammar: evaluate absolute
 * arithmetic and already-resolved local differences via the existing
 * Common parser. Do not fabricate sizes for forward/relocatable symbols. */
static bool emit_common_space(MiniAs *as,int section,size_t total,uint8_t fill) {
    /* Unlike raw .fill, GNU ignores nonzero .space/.skip in SHT_NOBITS.
     * One checked capacity reservation + memset replaces thousands of
     * 256-byte Section appends for large source-generated padding. */
    return minias_section_append_fill(as,section,total,fill,true);
}

static bool emit_common_pattern(MiniAs *as,int section,
                                const unsigned char *element,
                                size_t width,size_t repeat) {
    MiniAsSection *sec;
    size_t bytes,filled;
    unsigned char *output;
    size_t i;
    if(section<0 || (size_t)section>=as->section_count ||
       width==0U || width>8U || repeat>SIZE_MAX/width) {
        minias_set_error(as,"invalid-data-pattern");
        return false;
    }
    sec=&as->sections[(size_t)section];
    if(as->emitting_fragment && !as->committing_fragment)
        return minias_stage_fragment_pattern(
            as,section,element,width,repeat,false);
    bytes=width*repeat;
    if(bytes>SIZE_MAX-sec->size) {
        minias_set_error(as,"section-size-overflow:%s",sec->name);
        return false;
    }
    if(sec->type==MINIAS_SHT_NOBITS) {
        for(i=0U;i<width;++i)
            if(element[i]!=0U) {
                minias_set_error(as,"nobits-nonzero-data:%s",sec->name);
                return false;
            }
        sec->size+=bytes;
        return true;
    }
    if(!minias_section_reserve(as,sec,sec->size+bytes))return false;
    if(bytes!=0U) {
        output=sec->data+sec->size;
        /* A short seed can be doubled with non-overlapping copies. The
         * number of memcpy calls is logarithmic rather than O(repeat). */
        memcpy(output,element,width);
        filled=width;
        while(filled<bytes) {
            size_t chunk=filled<bytes-filled?filled:bytes-filled;
            memcpy(output+filled,output,chunk);
            filled+=chunk;
        }
    }
    sec->size+=bytes;
    return true;
}

/* One encoding contract for eagerly resolved LEB literals and the retained
 * deferred LEB fragments resolved by MCLayout. Neither route re-parses
 * expressions while materializing section bytes. */
bool minias_encode_leb_integer(int64_t number,bool signed_leb,
                               unsigned char output[10],size_t *length) {
    uint64_t bits;
    size_t n=0U;
    if(!signed_leb && number<0)return false;
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

/* An immediate LEB remains a parse-time immutable byte snapshot. Symbolic
 * expressions enter the same Fragment fixed-point as .fill/.space instead. */
static bool minias_leb_bytes(MiniAs *as,const char *op,const char *text,
                             unsigned char output[10],size_t *length) {
    int64_t number;
    return minias_evaluate_absolute_expression(as,text,&number) &&
           minias_encode_leb_integer(number,strcmp(op,".sleb128")==0,
                                     output,length);
}

/* Repeat a literal little-endian element: GNU .fill repeat[,size[,value]].
 * All targets use Common DATA fragments; no target-specific section writer. */
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
        if(minias_expr_symbol_start(*p)) {
            const char *start=p;
            size_t len;
            char name[256];
            MiniAsSymbol *sym;
            do {++p;} while(minias_expr_symbol_continue(*p));
            len=(size_t)(p-start);
            if(len>=sizeof(name))continue;
            memcpy(name,start,len);name[len]='\0';
            sym=minias_get_symbol(as,name,false);
            if(sym && sym->mutable_absolute_set)return true;
        }else ++p;
    }
    return false;
}

/* GAS snapshots mutable absolute .set operands at each lexical DATA site,
 * including symbolic .fill/.space sizes. A later .set reassignment cannot
 * retroactively change the earlier fragment during iterative layout. */
static bool snapshot_mutable_data_tokens(MiniAs *as,const char *input,
                                         char **out) {
    size_t original=strlen(input),capacity,used=0U;
    const char *p=input;
    char *result;
    if(original>(SIZE_MAX-1U)/24U) {
        minias_set_error(as,"mutable-data-expression-overflow");return false;
    }
    capacity=original*24U+1U;
    result=malloc(capacity);
    if(!result) {
        minias_set_error(as,"out-of-memory:mutable-data-snapshot");return false;
    }
    while(*p) {
        const char *start=p;
        char representation[48];
        const char *chosen=NULL;
        size_t part;
        if(isdigit((unsigned char)*p)) {
            char *end=NULL;uint64_t number;
            if(minias_parse_number_literal(p,&end,&number) && end>p) {
                p=end;
            }else ++p;
        }else if(minias_expr_symbol_start(*p)) {
            char name[256];MiniAsSymbol *sym;
            while(minias_expr_symbol_continue(*p))++p;
            part=(size_t)(p-start);
            if(part<sizeof(name)) {
                int n;
                memcpy(name,start,part);name[part]='\0';
                sym=minias_get_symbol(as,name,false);
                if(sym && sym->defined && sym->mutable_absolute_set &&
                   sym->section==MINIAS_SECTION_ABS) {
                    if(sym->value<=INT64_MAX)
                        n=snprintf(representation,sizeof(representation),
                                   "%llu",(unsigned long long)sym->value);
                    else
                        n=snprintf(representation,sizeof(representation),
                                   "-%llu",(unsigned long long)
                                   (UINT64_MAX-sym->value+UINT64_C(1)));
                    if(n<0 || (size_t)n>=sizeof(representation)) {
                        free(result);return false;
                    }
                    chosen=representation;
                }
            }
        }else ++p;
        part=chosen?strlen(chosen):(size_t)(p-start);
        if(part>=capacity-used) {
            minias_set_error(as,"mutable-data-snapshot-overflow");
            free(result);return false;
        }
        memcpy(result+used,chosen?chosen:start,part);
        used+=part;
    }
    result[used]='\0';*out=result;return true;
}

/* One Common semantic expression compiler for every target's
 * .byte/.short/.long/.quad/.inst data fields. The earlier syntax checker
 * and the final materializer no longer tokenize the same source again.
 * A deferred-absolute node is necessary for .set and local-dot arithmetic:
 * it is not a fabricated relocation or prematurely frozen section address.
 */
static bool add_data_stmt(MiniAs *as, const char *op, char *args, size_t line) {
    uint64_t count = 0U;
    uint64_t bytes;
    MiniAsFragmentKind fragment_kind=MINIAS_FRAGMENT_FIXED_DATA;
    unsigned int width = data_width(as, op);
    char *copy = NULL;
    char *cursor;
    char *snapshot=NULL;
    bool delayed=false;
    bool variable_parsed=false;
    uint64_t variable_repeat=0U,variable_value=0U;
    unsigned variable_width=0U;
    unsigned char leb_snapshot[10]={0};
    size_t leb_snapshot_size=0U;
    bool has_leb_snapshot=false;
    unsigned char *string_snapshot=NULL;
    size_t string_snapshot_size=0U;
    bool has_binary_snapshot=false;

    if (!strcmp(op,".fill")) {
        fragment_kind=MINIAS_FRAGMENT_FILL;
        unsigned elem=0U;
        uint64_t repeats=0U,value=0U;
        delayed=common_data_has_symbolic_token(args);
        if(!minias_parse_common_fill(as,args,&repeats,&elem,&value) ||
           (elem!=0U && repeats>UINT32_MAX/elem)) {
            delayed=true;
            /* Forward labels and subsection values have not received
             * their final layout yet. Keep this as a real data fragment
             * with provisional size 0, never as an ignored directive.
             * Invalid syntax/unresolved symbols are rejected at fixpoint. */
            bytes=0U;
        }else {
            bytes=repeats*elem;
            variable_parsed=true;
            variable_repeat=repeats;
            variable_value=value;
            variable_width=elem;
        }
    } else if (strcmp(op,".uleb128")==0 || strcmp(op,".sleb128")==0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        unsigned char coded[10];
        size_t len=0U;
        /* Forward numeric labels and DWARF view symbols are not yet
         * located. Their widths belong to the typed layout fixed point,
         * not to a speculative first-pass evaluation or source replay. */
        delayed=common_data_has_symbolic_token(args);
        if(delayed) {
            bytes=0U;
        }else if(!minias_leb_bytes(as,op,args,coded,&len)) {
            minias_set_error(as,"unsupported-leb-expression:%s:line=%zu",args,line);
            return false;
        }else {
            bytes=len;
            memcpy(leb_snapshot,coded,len);
            leb_snapshot_size=len;
            has_leb_snapshot=true;
        }
    } else if (strcmp(op, ".incbin") == 0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        if(!capture_incbin(as,args,line,&string_snapshot,
                           &string_snapshot_size))return false;
        bytes=(uint64_t)string_snapshot_size;
        has_binary_snapshot=true;
    } else if (strcmp(op, ".asciz") == 0 || strcmp(op, ".string") == 0 ||
        strcmp(op, ".ascii") == 0) {
        fragment_kind=MINIAS_FRAGMENT_BYTES;
        unsigned char *decoded = NULL;
        size_t decoded_size = 0U;
        bool terminate = strcmp(op, ".ascii") != 0;

        if (!minias_decode_string_literals(args, terminate, &decoded, &decoded_size)) {
            minias_set_error(as, "unsupported-string:%s:line=%zu", op, line);
            return false;
        }
        /* Retain decoded literal bytes: output must never re-decode a
         * source string after Fragment layout has changed. */
        string_snapshot=decoded;
        string_snapshot_size=decoded_size;
        bytes = (uint64_t)decoded_size;
    } else if (strcmp(op, ".zero") == 0 || strcmp(op, ".space") == 0 ||
               strcmp(op, ".skip") == 0) {
        fragment_kind=MINIAS_FRAGMENT_SPACE;
        uint8_t fill=0U;
        delayed=common_data_has_symbolic_token(args);
        if (!minias_parse_common_space(as,args,&count,&fill)) {
            delayed=true;
            /* .skip/.space also permits forward absolute label differences.
             * A zero-length provisional fragment is resolved and verified
             * after all symbols have been placed. */
            count=0U;
        }
        bytes = count;
        /* The solver revisits all deferred fragments. The parse-site
         * value below is immutable when no symbolic operands exist. */
        if(!delayed) {
            variable_parsed=true;
            variable_repeat=count;
            variable_value=fill;
            variable_width=1U;
        }
    } else {
        /* One Common MCExpr parser validates every fixed-width field.
         * Measurement only counts fields; parsed semantic expressions are
         * retained on the stable DATA Fragment for emission. */
        if(width==0U) {
            minias_set_error(as,"invalid-fixed-data-width:%s",op);
            return false;
        }
        count=1U;
        for(cursor=args;*cursor;++cursor)
            if(*cursor==',')++count;
        if(count>UINT32_MAX/width) {
            minias_set_error(as,"mc-data-count-overflow:line=%zu",line);
            return false;
        }
        bytes=count*width;
    }

    if (bytes > UINT32_MAX) {
        free(string_snapshot);
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
                }else if(minias_expr_parse_symbol_absolute_tail(as,token,&reloc)) {
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
    if(!snapshot && as->has_mutable_absolute_sets &&
       (fragment_kind==MINIAS_FRAGMENT_FILL ||
        fragment_kind==MINIAS_FRAGMENT_SPACE ||
        (fragment_kind==MINIAS_FRAGMENT_BYTES && delayed)) &&
       !snapshot_mutable_data_tokens(as,args,&snapshot))return false;
    if (!add_stmt(as, fragment_kind, op,
                  snapshot?snapshot:args, line, (uint32_t)bytes, 1U)) {
        free(snapshot);
        free(string_snapshot);
        return false;
    }
    free(snapshot);
    if(has_leb_snapshot || has_binary_snapshot || string_snapshot!=NULL) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        if(has_leb_snapshot) {
            stmt->static_data=malloc(leb_snapshot_size);
            if(!stmt->static_data) {
                minias_set_error(as,"out-of-memory:leb-snapshot:line=%zu",line);
                return false;
            }
            memcpy(stmt->static_data,leb_snapshot,leb_snapshot_size);
            stmt->static_data_size=leb_snapshot_size;
        }else {
            stmt->static_data=string_snapshot;
            stmt->static_data_size=string_snapshot_size;
            string_snapshot=NULL;
        }
    }
    if(fragment_kind==MINIAS_FRAGMENT_BYTES &&
       (strcmp(op,".uleb128")==0 || strcmp(op,".sleb128")==0))
        as->stmts[as->stmt_count-1U].leb_mode=
            strcmp(op,".sleb128")==0?2U:1U;
    if(variable_parsed) {
        MiniAsStmt *fragment=&as->stmts[as->stmt_count-1U];
        fragment->variable_data_resolved=true;
        fragment->variable_repeat=variable_repeat;
        fragment->variable_value=variable_value;
        fragment->variable_element_width=variable_width;
    }
    if(width!=0U &&
       !minias_compile_data_mc(as,&as->stmts[as->stmt_count-1U],width))
        return false;
    if((fragment_kind==MINIAS_FRAGMENT_FILL ||
        fragment_kind==MINIAS_FRAGMENT_SPACE) &&
       !minias_compile_variable_exprs(as,&as->stmts[as->stmt_count-1U]))
        return false;
    if(fragment_kind==MINIAS_FRAGMENT_BYTES && delayed) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        stmt->variable_exprs[0]=minias_compile_deferred_expr(as,stmt->args);
        if(stmt->variable_exprs[0]==NULL)return false;
        stmt->variable_expr_count=1U;
        /* The retained payload changes size during the Fragment fixed
         * point; allocate its maximum encoded width only once. */
        stmt->static_data=malloc(10U);
        if(!stmt->static_data) {
            minias_set_error(as,"out-of-memory:deferred-leb:line=%zu",line);
            return false;
        }
    }
    if(delayed) {
        as->stmts[as->stmt_count-1U].deferred_data_size=true;
        as->has_deferred_data=true;
        /* A deferred size is a persistent Fragment property. It need not
         * trigger a second source parse: the shared layout solver can
         * recompute it after branch growth using final symbol anchors.
         * Only controls that change the Fragment graph, notably
         * location-dependent .if/.elseif, retain the reparse guard.
         * Typed .org/.fill/.space are resolved in-place by MCLayout. */
    }
    return advance_current_location(as, bytes);
}

static bool emit_data_stmt(MiniAs *as, const MiniAsStmt *stmt) {
    /* Source spelling is intentionally absent from this dispatcher.
     * Every data producer, including synthesized literal pools, installs
     * its semantic FragmentKind while parsing, once and for all. */
    switch(stmt->fragment_kind) {
    case MINIAS_FRAGMENT_FILL: {
        uint64_t repeat=stmt->variable_repeat;
        uint64_t value=stmt->variable_value;
        unsigned width_bytes=stmt->variable_element_width,j;
        unsigned char element[8];
        if(!stmt->variable_data_resolved || width_bytes>8U ||
           (width_bytes!=0U &&
            (repeat>UINT32_MAX/width_bytes ||
             repeat*width_bytes!=(uint64_t)stmt->size))) {
            minias_set_error(as,"invalid-typed-fill-fragment:line=%zu",
                             stmt->line);
            return false;
        }
        if(width_bytes==0U)return stmt->size==0U;
        for(j=0U;j<width_bytes;++j)
            element[j]=(unsigned char)(value>>(8U*j));
        return emit_common_pattern(as,stmt->section,element,width_bytes,
                                   (size_t)repeat);
    }
    case MINIAS_FRAGMENT_BYTES:
        if(stmt->static_data_size!=stmt->size ||
           (stmt->size!=0U && !stmt->static_data)) {
            minias_set_error(as,"immutable-data-fragment-size:line=%zu",
                             stmt->line);
            return false;
        }
        /* DATA emission consumes the owned byte snapshot (lexical bytes or
         * fixed-point-resolved LEB), never reinterpreting source text. */
        return minias_section_append(as,stmt->section,
                                     stmt->static_data,stmt->static_data_size);
    case MINIAS_FRAGMENT_SPACE:
        if(!stmt->variable_data_resolved ||
           stmt->variable_element_width!=1U ||
           stmt->variable_repeat!=(uint64_t)stmt->size ||
           stmt->variable_value>255U) {
            minias_set_error(as,"invalid-typed-space-fragment:line=%zu",
                             stmt->line);
            return false;
        }
        return emit_common_space(as,stmt->section,stmt->size,
                                 (uint8_t)stmt->variable_value);
    case MINIAS_FRAGMENT_FIXED_DATA:
        return minias_emit_mc_data(as,stmt);
    default:
        minias_set_error(as,"unexpected-data-fragment-kind:%u:line=%zu",
                         (unsigned)stmt->fragment_kind,stmt->line);
        return false;
    }
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
                {
                    char kind[40];
                    written=snprintf(kind,sizeof(kind),"num_%zu",label_id);
                    if(written<0 || (size_t)written>=sizeof(kind) ||
                       !minias_make_private_symbol_name(
                           replacement,sizeof(replacement),kind,generation))
                        goto oom;
                }
                written=(int)strlen(replacement);
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
    if (!minias_grow_array((void **)&as->section_stack,
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
    return minias_ensure_subsection(as, as->current_section, as->current_subsection) != NULL;
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
        {
            char kind[40];
            written=snprintf(kind,sizeof(kind),"num_%zu",numeric_id);
            if(written<0 || (size_t)written>=sizeof(kind) ||
               !minias_make_private_symbol_name(
                   canonical,sizeof(canonical),kind,generation)) {
                minias_set_error(as,"numeric-label-too-long:line=%zu",line);
                return false;
            }
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
    if (!minias_current_location(as, &symbol->value)) {
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
    const bool width_order=as->target->literal_pool_order==
                           MINIAS_LITERAL_POOL_WIDTH_ASC;
    /* GNU GAS scheduling is TargetInfo policy, not a hard-coded AArch64
     * rule inside the Common assembler.  Each selected entry becomes an
     * owned ALIGN fragment, symbol anchor, and typed DATA fragment below;
     * no synthetic .balign/.long/.quad source text is reparsed. */
    for(width=4U;width<=8U;width*=2U) {
      for(i=0U;i<as->literal_count;++i) {
        MiniAsLiteral *entry=&as->literals[i];
        MiniAsStmt *fragment;
        MiniAsMCData *data;
        MiniAsSymbol *target;
        uint64_t current,pad;
        if(width_order && entry->width!=width)continue;
        if(!width_order && width!=4U)continue;
        if(entry->emitted ||
           (!all && (entry->section!=as->current_section ||
                     entry->subsection!=as->current_subsection)))continue;
        if(all) {
            as->current_section=entry->section;
            as->current_subsection=entry->subsection;
            if(!minias_ensure_subsection(as,as->current_section,as->current_subsection))
                goto failure;
        }
        if(!minias_current_location(as,&current))goto failure;
        pad=((uint64_t)entry->width-(current&((uint64_t)entry->width-1U)))&
            ((uint64_t)entry->width-1U);
        if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,".literal_pool_align","",line,
                     (uint32_t)pad,entry->width))goto failure;
        fragment=&as->stmts[as->stmt_count-1U];
        fragment->align_has_fill=true;
        fragment->align_fill=0U;
        if((uint64_t)entry->width>as->sections[(size_t)entry->section].align)
            as->sections[(size_t)entry->section].align=entry->width;
        if(!advance_current_location(as,pad) ||
           !define_label(as,entry->label,line))goto failure;

        /* Literal payload is a standard typed MCExpr+SymbolID. Data bytes
         * and relocation fixups follow exactly the ordinary DATA emitter;
         * the pool cannot depend on another pass over synthetic strings. */
        if(!add_stmt(as,MINIAS_FRAGMENT_FIXED_DATA,".literal_pool_data","",line,
                     entry->width,1U))goto failure;
        fragment=&as->stmts[as->stmt_count-1U];
        data=calloc(1U,sizeof(*data));
        if(data==NULL) {
            minias_set_error(as,"out-of-memory:literal-pool-data");
            goto failure;
        }
        data->fields=calloc(1U,sizeof(*data->fields));
        if(data->fields==NULL) {
            free(data);
            minias_set_error(as,"out-of-memory:literal-pool-fields");
            goto failure;
        }
        data->width=entry->width;data->count=1U;
        if(entry->expression.name_id>=as->symbol_name_count ||
           as->symbol_names[entry->expression.name_id].name==NULL) {
            minias_set_error(as,"literal-pool-symbol-ref-invalid");
            minias_mcdata_destroy(data);goto failure;
        }
        target=minias_get_symbol(as,
                     as->symbol_names[entry->expression.name_id].name,true);
        if(target==NULL) {minias_mcdata_destroy(data);goto failure;}
        data->fields[0].kind=MINIAS_MC_EXPR_SYMBOL;
        data->fields[0].has_lhs=true;
        data->fields[0].lhs_symbol_index=(size_t)(target-as->symbols);
        data->fields[0].lhs_addend=entry->expression.addend;
        fragment->mcdata=data;
        if(!advance_current_location(as,entry->width))goto failure;
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
    if(!minias_expr_symbol_start(*name) || !*size_text)goto invalid;
    for(k=1U;name[k];++k)
        if(!minias_expr_symbol_continue(name[k]))goto invalid;
    if(!minias_evaluate_absolute_expression(as,size_text,&size) || size<0)
        goto invalid;
    if(align_text) {
        if(!minias_evaluate_absolute_expression(as,align_text,&alignment_value) ||
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
        ok=minias_ensure_subsection(as,bss,0U)!=NULL &&
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
       minias_evaluate_absolute_expression(as,offset_text,&offset) && offset>=0 &&
       minias_parse_symbol_addend(symbol,&expression);
    if(ok)ok=minias_add_section_fixup(as,
                 MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION,
                 as->current_section,(uint64_t)offset,
                 kind,width,expression.name,expression.addend);
    free(copy);
    if(ok)return true;
invalid:
    minias_set_error(as,"bad-directive:.reloc:%s:line=%zu",args,line);
    return false;
}

/* GAS `.ident` is NOT parser metadata. It emits a mergeable string in the
 * ELF .comment section. If `.ident` creates the section, GAS seeds it with
 * one leading NUL; if source code already created .comment, no seed is
 * added. The payload is ordinary immutable MC DATA, so it participates in
 * the exact same Section/Subsection/Layout/ELF lifetime as all other data.
 * An implicit section switch must not change `.previous` or the current
 * source subsection. */
static bool handle_ident(MiniAs *as,char *args,size_t line) {
    const int prior=minias_find_section(as,".comment");
    const int section=minias_ensure_section(as,".comment",
                      MINIAS_SHT_PROGBITS,
                      MINIAS_SHF_MERGE|MINIAS_SHF_STRINGS,1U);
    const int saved_section=as->current_section;
    const uint32_t saved_subsection=as->current_subsection;
    bool ok;
    char seed[]="1";
    if(section<0)return false;
    /* GAS upgrades an existing .comment to SHF_MERGE|SHF_STRINGS and
     * sh_entsize=1 when `.ident` is used, preserving any explicit flags
     * such as SHF_ALLOC. A later section declaration does not undo that. */
    as->sections[(size_t)section].flags |=
        MINIAS_SHF_MERGE|MINIAS_SHF_STRINGS;
    as->sections[(size_t)section].entry_size=1U;
    as->current_section=section;
    as->current_subsection=0U;
    ok=minias_ensure_subsection(as,section,0U)!=NULL &&
       (prior>=0 || add_data_stmt(as,".zero",seed,line)) &&
       add_data_stmt(as,".asciz",args,line);
    as->current_section=saved_section;
    as->current_subsection=saved_subsection;
    if(!ok && as->error[0]=='\0')
        minias_set_error(as,"bad-directive:.ident:line=%zu",line);
    return ok;
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
    bool had_leading_label=false;
    MiniAsCommonDirectiveKind directive_kind=MINIAS_COMMON_DIRECTIVE_OTHER;

    text = minias_trim(text);
    if (*text == '\0') {
        return true;
    }

    /* GNU GCC/Clang inline-assembler boundaries are exact standalone
     * source markers. AArch64 uses # for immediate operands, so do not
     * treat arbitrary #foo or #number as a comment. */
    if (strcmp(text, "#APP") == 0 || strcmp(text, "#NO_APP") == 0 ||
        strcmp(text, "#NOAPP") == 0) return true;
    /* A leading '# ' begins a GNU preprocessor line or comment; a target
     * immediate such as '#42' is still an operand, never a comment. */
    if(text[0]=='#' && (text[1]=='#' ||
                        isspace((unsigned char)text[1])))return true;

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
        return push_conditional_value(as,nonblank?*arg!='\0':*arg=='\0');
    }
    if ((strncmp(text,".ifeqs",6U)==0 ||
         strncmp(text,".ifnes",6U)==0) &&
        (text[6]=='\0' || isspace((unsigned char)text[6]))) {
        bool unequal=text[3]=='n';
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
                int64_t literal;
                if (!minias_eval_i64_expression(text+width,&literal))
                    as->layout_sensitive=true;
                return push_conditional_numeric(as,
                           minias_trim(text+width),name,line);
            }
        }
    }
    if (strncmp(text, ".if", 3U) == 0 &&
        (text[3] == '\0' || isspace((unsigned char)text[3]))) {
        /* A literal condition is a lexical choice.  Symbol/dot-dependent
         * control flow can change its emitted Fragment set after layout. */
        int64_t literal;
        if (!minias_eval_i64_expression(text+3U,&literal))
            as->layout_sensitive = true;
        return push_conditional(as, minias_trim(text + 3), line);
    }
    if (strncmp(text, ".elseif", 7U) == 0 &&
        (text[7] == '\0' || isspace((unsigned char)text[7]))) {
        int64_t literal;
        if (!minias_eval_i64_expression(text+7U,&literal))
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
        if (!is_persistent_symbol_assignment(rhs))
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

    if (strcmp(op, ".set") == 0 || strcmp(op, ".equ") == 0) {
        const char *comma = strchr(args, ',');
        if (comma == NULL ||
            !is_persistent_symbol_assignment(comma + 1U))
            as->layout_sensitive = true;
    }
    /* Space/skip/zero depend on Fragment layout, not parser control flow:
     * the shared MCLayout fixed point resolves dot and forward symbols.
     * Dot-dependent alignment grammar remains conservatively reparsed. */
    if ((strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
         strcmp(op, ".p2align") == 0) &&
        strchr(args, '.') != NULL) {
        as->layout_sensitive = true;
    }
    /* Target syntax can be a dotted directive (`.option`) or a bare
     * identifier followed by a target directive (`alias .req x0`). Common
     * cannot enumerate the latter forms without leaking ISA grammars. Ask
     * the owning Target first for EVERY non-macro statement; an unhandled
     * instruction then reaches the mandatory parsed-MCInst interface. */
    if (as->target->parse_target_directive != NULL) {
        bool handled = false;
        if (!as->target->parse_target_directive(as, op, args,
                                                line, &handled)) {
            return false;
        }
        if (handled) return true;
    }
    if (op[0] == '.') {
        directive_kind=common_directive_kind(as,op);
        if (as->target->validate_common_directive != NULL &&
            !as->target->validate_common_directive(as,directive_kind,
                                                   op,line))
            return false;
    }
    if (op[0] == '.' && strcmp(op, ".insn") != 0) {
        if (strcmp(op, ".cfi_sections") == 0 ||
            ((as->cfi_debug_frame || as->cfi_eh_frame) &&
             strncmp(op, ".cfi_", 5U) == 0) ||
            (as->target->cfi_implicit_eh_frame &&
             strncmp(op, ".cfi_", 5U) == 0)) {
            /* Every enabled backend must emit real .eh_frame records;
             * silent CFI boundary-only acceptance is not correctness. */
            if (as->target->cfi_implicit_eh_frame &&
                !as->cfi_debug_frame && !as->cfi_eh_frame &&
                strcmp(op, ".cfi_sections") != 0)
                as->cfi_eh_frame = true;
            return minias_cfi_directive(as,op,args,line);
        }
        if (!strcmp(op,".file") || !strcmp(op,".loc"))
            return minias_debug_directive(as,op,args,line);
        /* Never accept CFI boundaries without a CIE/FDE output plan. A
         * fifth Target lacking implicit EH must declare .cfi_sections or
         * explicitly reject the source, not silently lose unwind data. */
        if (strncmp(op,".cfi_",5U)==0) {
            minias_set_error(as,"unsupported-target-cfi:%s:line=%zu",op,line);
            return false;
        }
        if (strcmp(op, ".text") == 0) {
            return switch_section(as,
                                  ".text",
                                  NULL,
                                  NULL,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR,
                                  minias_target_exec_alignment(as));
        }
        if (strcmp(op, ".data") == 0) {
            return switch_section(as,
                                  ".data",
                                  NULL,
                                  NULL,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".bss") == 0) {
            return switch_section(as,
                                  ".bss",
                                  NULL,
                                  NULL,
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
            return minias_ensure_subsection(as,
                                     as->current_section,
                                     as->current_subsection) != NULL;
        }
        if (strcmp(op, ".subsection") == 0) {
            uint64_t number;
            if (!minias_parse_u64(args, &number) || number > 8192U) {
                minias_set_error(as, "bad-directive:.subsection:%s:line=%zu", args, line);
                return false;
            }
            if ((uint32_t)number != as->current_subsection) {
                as->previous_section = as->current_section;
                as->previous_subsection = as->current_subsection;
                as->current_subsection = (uint32_t)number;
            }
            return minias_ensure_subsection(as,
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
            return true;
        }
        if (strcmp(op, ".ident") == 0) {
            return handle_ident(as,args,line);
        }
        if (strcmp(op, ".end") == 0) {
            /* GAS ignores a trailing .end operand but stops the stream. */
            as->source_ended=true;
            return true;
        }
        if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
            strcmp(op, ".p2align") == 0) {
            return handle_align(as, op, args, line);
        }
        if (directive_kind == MINIAS_COMMON_DIRECTIVE_ORG) {
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
        if (directive_kind == MINIAS_COMMON_DIRECTIVE_RAW_DATA) {
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
        /* ISA/ABI validation is owned by the target's one typed parser.
         * The obsolete pre-parse validation callback had no live Target. */
        ok = add_stmt(as, MINIAS_FRAGMENT_INSTRUCTION, op, rewritten_args,
                      line, 0U, 1U);
        if (ok)
            ok = as->target->measure_mcinst(
                as, &as->stmts[statement_id], &size);
        if (!ok) {
            free(rewritten_args);
            return false;
        }
        size_t forced_minimum_size=0U;
        if(!source_graph_lookup_forced_branch(as,
                                              &as->stmts[statement_id],
                                              &forced_minimum_size)) {
            free(rewritten_args);
            return false;
        }
        /* A source-replayed branch keeps its monotone minimum *alternative*,
         * not merely a boolean "forced long" bit.  RV C16 -> B32 -> long8
         * needs two reparses when a dot-sensitive .set/.if is present.
         * Each step is still supplied by the Target's typed MC plan. */
        while (size<forced_minimum_size) {
            MiniAsTargetRelaxPlan plan;
            as->stmts[statement_id].size=size;
            if (!minias_query_target_relax_plan(
                    as,&as->stmts[statement_id],&plan) ||
                plan.terminal_size<=size ||
                plan.terminal_size>forced_minimum_size) {
                minias_set_error(as, "invalid-target-relaxation:%s:line=%zu",
                                 op, line);
                free(rewritten_args);
                return false;
            }
            size = plan.terminal_size;
        }
        as->stmts[statement_id].size = size;
        free(rewritten_args);
    }
    return advance_current_location(as, size);
}

typedef struct MiniAsSourceLine {
    char *text;
    size_t line;
    size_t source_segment;
} MiniAsSourceLine;

/* The physical file and each expanded source body are processed by the
 * same macro/REPT/IRP collector. A SourceFrame owns only its lexical text;
 * the assembler retains typed Fragment/Symbol data independently of it.
 * Later conditional-region invalidation can retain this frame instead of
 * depending on a second physical-file parser with different state rules. */
typedef enum MiniAsSourceFrameKind {
    MINIAS_SOURCE_FRAME_FILE,
    MINIAS_SOURCE_FRAME_EXPANSION
} MiniAsSourceFrameKind;

typedef enum MiniAsSourceSiteKind {
    MINIAS_SOURCE_SITE_LINE = 1,
    MINIAS_SOURCE_SITE_SEGMENT,
    MINIAS_SOURCE_SITE_MACRO,
    MINIAS_SOURCE_SITE_REPT,
    MINIAS_SOURCE_SITE_IRP,
    MINIAS_SOURCE_SITE_INLINE_REPEAT
} MiniAsSourceSiteKind;

/* Full-key equality, not a truncated path hash, defines StableSiteID.
 * Parent is an already-interned lexical/expansion SiteID; an expansion
 * child's identity is unaffected by GNU's independently incrementing \@. */
typedef struct MiniAsSourceSiteKey {
    MiniAsStableSiteId parent;
    MiniAsStableSiteId definition;
    size_t origin_line;
    size_t origin_segment;
    size_t iteration;
    MiniAsSourceSiteKind kind;
} MiniAsSourceSiteKey;

typedef struct MiniAsSourceSiteEntry {
    MiniAsSourceSiteKey key;
    size_t last_condition_pass;
    MiniAsStableSiteId last_condition_owner;
    bool last_condition_active;
    /* Zero means that this lexical/expansion site has no forced branch.
     * The index is stable across complete semantic replays; a pass-local
     * Fragment ordinal is never persisted in this registry. */
    size_t forced_branch_index;
} MiniAsSourceSiteEntry;

/* The source site identifies one expanded instruction, while this retained
 * shape prevents a branch from being accidentally attached to a different
 * instruction after a .if/.elseif changes the emitted Fragment topology.
 * All fields here are semantic parse-time values, never an output offset. */
typedef struct MiniAsForcedOperandShape {
    MiniAsMCOperandKind kind;
    MiniAsMCRegisterClass reg_class;
    uint16_t target_reg_bank;
    unsigned reg;
    bool register_64;
} MiniAsForcedOperandShape;

typedef struct MiniAsForcedBranchSite {
    MiniAsStableSiteId source_site;
    char *op;
    uint32_t opcode;
    uint32_t target_form;
    uint32_t mode_bits;
    size_t minimum_size;
    unsigned operand_count;
    MiniAsForcedOperandShape *operands;
} MiniAsForcedBranchSite;

typedef struct MiniAsSourceFrame {
    MiniAsSourceFrameKind kind;
    MiniAsSourceLine *lines;
    size_t count;
    size_t capacity;
    MiniAsSourceSiteKind expansion_kind;
    MiniAsStableSiteId invocation_site;
    MiniAsStableSiteId definition_site;
    size_t iteration;
} MiniAsSourceFrame;

struct MiniAsSourceGraph {
    MiniAsSourceFrame physical;
    MiniAsSourceSiteEntry *sites;
    size_t site_count;
    size_t site_capacity;
    size_t *site_slots; /* SiteID, zero for unoccupied */
    size_t site_slot_capacity;
    size_t expansion_count;
    size_t file_reads;
    size_t parse_passes;
    size_t conditional_changes;
    size_t ordinal_drifts;
    MiniAsForcedBranchSite *forced_branches;
    size_t forced_branch_count;
    size_t forced_branch_capacity;
};

static void destroy_source_lines(MiniAsSourceLine *lines, size_t count);

static size_t source_site_hash(MiniAsSourceSiteKey key) {
    size_t hash=(size_t)UINT64_C(0x9e3779b97f4a7c15);
    const size_t values[]={key.parent,key.definition,key.origin_line,
                           key.origin_segment,key.iteration,(size_t)key.kind};
    size_t i;
    for(i=0U;i<sizeof(values)/sizeof(values[0]);++i) {
        size_t item=values[i];
        item^=item>>15U;
        item*=((size_t)UINT32_C(0x85ebca6b));
        item^=item>>13U;
        hash^=item+(size_t)UINT32_C(0x9e3779b9)+(hash<<6U)+(hash>>2U);
    }
    return hash;
}

static bool source_site_same(MiniAsSourceSiteKey a,
                             MiniAsSourceSiteKey b) {
    return a.parent==b.parent && a.definition==b.definition &&
           a.origin_line==b.origin_line &&
           a.origin_segment==b.origin_segment &&
           a.iteration==b.iteration && a.kind==b.kind;
}

static bool source_graph_rehash(MiniAsSourceGraph *graph,size_t capacity) {
    size_t *slots=calloc(capacity,sizeof(*slots));
    size_t i;
    if(slots==NULL)return false;
    for(i=0U;i<graph->site_count;++i) {
        size_t at=source_site_hash(graph->sites[i].key)&(capacity-1U);
        while(slots[at]!=0U)at=(at+1U)&(capacity-1U);
        slots[at]=i+1U;
    }
    free(graph->site_slots);
    graph->site_slots=slots;
    graph->site_slot_capacity=capacity;
    return true;
}

static bool source_graph_intern(MiniAs *as,MiniAsSourceSiteKey key,
                                MiniAsStableSiteId *site_id) {
    MiniAsSourceGraph *graph=as->source_graph;
    size_t slot,probes;
    size_t hashed=source_site_hash(key);
    if(graph==NULL || site_id==NULL ||
       key.parent>graph->site_count ||
       key.definition>graph->site_count ||
       graph->site_count==SIZE_MAX) {
        minias_set_error(as,"invalid-source-site-key");return false;
    }
    /* Most source-site visits are repeats across complete semantic replays.
     * Probe first so a pure lookup cannot accidentally trigger a costly
     * growth/rehash exactly at the half-full threshold. */
    if(graph->site_slot_capacity!=0U) {
        slot=hashed&(graph->site_slot_capacity-1U);
        for(probes=0U;probes<graph->site_slot_capacity;++probes) {
            size_t id=graph->site_slots[slot];
            if(id==0U)break;
            if(source_site_same(graph->sites[id-1U].key,key)) {
                *site_id=id;
                return true;
            }
            slot=(slot+1U)&(graph->site_slot_capacity-1U);
        }
    }
    if(graph->site_slot_capacity==0U ||
       graph->site_count+1U>=graph->site_slot_capacity/2U) {
        size_t next=graph->site_slot_capacity==0U?64U:
                    graph->site_slot_capacity*2U;
        if(next<graph->site_slot_capacity ||
           !source_graph_rehash(graph,next)) {
            minias_set_error(as,"out-of-memory:source-site-index");
            return false;
        }
    }
    slot=hashed&(graph->site_slot_capacity-1U);
    for(probes=0U;probes<graph->site_slot_capacity;++probes) {
        if(graph->site_slots[slot]==0U)break;
        slot=(slot+1U)&(graph->site_slot_capacity-1U);
    }
    if(probes==graph->site_slot_capacity ||
       !minias_grow_array((void **)&graph->sites,&graph->site_capacity,
                          sizeof(*graph->sites),graph->site_count+1U)) {
        minias_set_error(as,"out-of-memory:source-site");return false;
    }
    memset(&graph->sites[graph->site_count],0,sizeof(*graph->sites));
    graph->sites[graph->site_count].key=key;
    *site_id=++graph->site_count;
    graph->site_slots[slot]=*site_id;
    if(key.kind==MINIAS_SOURCE_SITE_MACRO ||
       key.kind==MINIAS_SOURCE_SITE_REPT ||
       key.kind==MINIAS_SOURCE_SITE_IRP ||
       key.kind==MINIAS_SOURCE_SITE_INLINE_REPEAT)
        ++graph->expansion_count;
    return true;
}

static bool source_graph_line_site(MiniAs *as,
                                   const MiniAsSourceLine *line,
                                   size_t segment,
                                   MiniAsStableSiteId *site_id) {
    const MiniAsSourceSiteKey key={
        .parent=as->current_expansion_site,
        .origin_line=line->line,
        .origin_segment=line->source_segment,
        .iteration=segment,
        .kind=segment==0U ? MINIAS_SOURCE_SITE_LINE :
                             MINIAS_SOURCE_SITE_SEGMENT
    };
    return source_graph_intern(as,key,site_id);
}

static void source_graph_record_condition(MiniAs *as,
                                          MiniAsStableSiteId owner,
                                          bool active) {
    MiniAsSourceGraph *graph=as->source_graph;
    MiniAsSourceSiteEntry *entry;
    if(graph==NULL || as->current_source_site==MINIAS_SITE_NONE ||
       as->current_source_site>graph->site_count)return;
    entry=&graph->sites[as->current_source_site-1U];
    if(entry->last_condition_pass!=0U &&
       entry->last_condition_pass+1U==graph->parse_passes &&
       (entry->last_condition_active!=active ||
        entry->last_condition_owner!=owner))
        ++graph->conditional_changes;
    entry->last_condition_pass=graph->parse_passes;
    entry->last_condition_owner=owner;
    entry->last_condition_active=active;
}

static void source_graph_destroy(MiniAsSourceGraph *graph) {
    size_t i;
    if(graph==NULL)return;
    destroy_source_lines(graph->physical.lines,graph->physical.count);
    free(graph->sites);
    free(graph->site_slots);
    for(i=0U;i<graph->forced_branch_count;++i) {
        free(graph->forced_branches[i].op);
        free(graph->forced_branches[i].operands);
    }
    free(graph->forced_branches);
    memset(graph,0,sizeof(*graph));
}

static bool source_graph_forced_shape_matches(
                           const MiniAsForcedBranchSite *saved,
                           const MiniAsStmt *stmt) {
    const MiniAsMCInst *inst=stmt->mcinst;
    unsigned i;
    if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION || inst==NULL ||
       stmt->source_site!=saved->source_site ||
       strcmp(stmt->op,saved->op)!=0 ||
       inst->opcode!=saved->opcode ||
       inst->target_form!=saved->target_form ||
       stmt->site.mode_bits!=saved->mode_bits ||
       inst->count>inst->capacity ||
       inst->count!=saved->operand_count)return false;
    /* Operand spelling is NOT a stable source identity. GNU 1f/1b refs
     * become private generation names, while macro \@ counters can shift
     * when an earlier conditional inserts a new macro expansion. The same
     * lexical SourceSite and typed opcode/form/register classes remain
     * stable; all symbol values are rebound by the fresh MC parser. */
    for(i=0U;i<inst->count;++i) {
        const MiniAsMCOperand *operand=&inst->operands[i];
        const MiniAsForcedOperandShape *shape=&saved->operands[i];
        if(operand->kind!=shape->kind ||
           operand->reg_class!=shape->reg_class ||
           operand->target_reg_bank!=shape->target_reg_bank ||
           operand->reg!=shape->reg ||
           operand->register_64!=shape->register_64)return false;
    }
    return true;
}

/* Parse-time branch growth has to consult the lexical identity, NOT the
 * statement's position in a newly generated vector. A different instruction
 * at the same site or a reused expansion identity is an error, even when its
 * current branch size happens to match the previous pass. */
static bool source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              size_t *minimum_size) {
    MiniAsSourceGraph *graph=as->source_graph;
    size_t encoded;
    *minimum_size=0U;
    if(graph==NULL)return true;
    if(stmt->source_site==MINIAS_SITE_NONE ||
       stmt->source_site>graph->site_count) {
        minias_set_error(as,"invalid-branch-source-site");
        return false;
    }
    encoded=graph->sites[stmt->source_site-1U].forced_branch_index;
    if(encoded==0U)return true;
    if(encoded>graph->forced_branch_count ||
       !source_graph_forced_shape_matches(&graph->forced_branches[encoded-1U],
                                         stmt)) {
        ++graph->ordinal_drifts;
        minias_set_error(as,"changed-forced-branch-shape:line=%zu",stmt->line);
        return false;
    }
    *minimum_size=graph->forced_branches[encoded-1U].minimum_size;
    return true;
}

/* Rebind every retained source identity to a fresh, pass-local Fragment ID.
 * One SourceSite may not ambiguously denote multiple machine instructions.
 * Missing branches (e.g. a changed conditional arm) fail CLOSED rather than
 * forcing an unrelated instruction that inherited its old ordinal. */
static bool source_graph_rebind_forced_branches(MiniAs *as,
                                                size_t **ids,
                                                size_t *count,
                                                size_t *capacity) {
    MiniAsSourceGraph *graph=as->source_graph;
    size_t *site_to_stmt;
    size_t i;
    if(graph==NULL || graph->forced_branch_count==0U) {
        *count=0U;
        return true;
    }
    if(graph->site_count==SIZE_MAX ||
       graph->site_count+1U>SIZE_MAX/sizeof(*site_to_stmt)) {
        minias_set_error(as,"forced-source-index-overflow");
        return false;
    }
    site_to_stmt=calloc(graph->site_count+1U,sizeof(*site_to_stmt));
    if(site_to_stmt==NULL) {
        minias_set_error(as,"out-of-memory:forced-source-index");
        return false;
    }
    for(i=0U;i<as->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        size_t *slot;
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION)continue;
        if(stmt->source_site==MINIAS_SITE_NONE ||
           stmt->source_site>graph->site_count) {
            minias_set_error(as,"invalid-source-instruction-site");
            free(site_to_stmt);
            return false;
        }
        slot=&site_to_stmt[stmt->source_site];
        *slot=*slot==0U?i+1U:SIZE_MAX;
    }
    if(!minias_grow_array((void **)ids,capacity,sizeof(**ids),
                          graph->forced_branch_count)) {
        minias_set_error(as,"out-of-memory:rebound-branch-ids");
        free(site_to_stmt);
        return false;
    }
    for(i=0U;i<graph->forced_branch_count;++i) {
        const MiniAsForcedBranchSite *saved=&graph->forced_branches[i];
        size_t entry=saved->source_site<=graph->site_count ?
            site_to_stmt[saved->source_site] : 0U;
        if(entry==0U || entry==SIZE_MAX ||
           !source_graph_forced_shape_matches(saved,&as->stmts[entry-1U])) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"lost-or-ambiguous-forced-source-site:%zu",
                             (size_t)saved->source_site);
            free(site_to_stmt);
            return false;
        }
        (*ids)[i]=entry-1U;
    }
    free(site_to_stmt);
    *count=graph->forced_branch_count;
    return true;
}

static bool source_graph_capture_forced_branches(MiniAs *as,
                                                 const size_t *ids,
                                                 size_t count) {
    MiniAsSourceGraph *graph=as->source_graph;
    size_t i;
    if(graph==NULL)return true;
    if(count<graph->forced_branch_count) {
        minias_set_error(as,"lost-forced-branch-after-layout");
        return false;
    }
    if(!minias_grow_array((void **)&graph->forced_branches,
                          &graph->forced_branch_capacity,
                          sizeof(*graph->forced_branches),count)) {
        minias_set_error(as,"out-of-memory:forced-source-sites");
        return false;
    }
    for(i=0U;i<count;++i) {
        const MiniAsStmt *stmt;
        MiniAsForcedBranchSite *saved;
        MiniAsTargetRelaxPlan plan;
        unsigned j;
        if(ids[i]>=as->stmt_count ||
           as->stmts[ids[i]].fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
           as->stmts[ids[i]].mcinst==NULL ||
           as->stmts[ids[i]].mcinst->count>
               as->stmts[ids[i]].mcinst->capacity) {
            minias_set_error(as,"invalid-forced-branch-fragment:%zu",ids[i]);
            return false;
        }
        stmt=&as->stmts[ids[i]];
        if(stmt->source_site==MINIAS_SITE_NONE ||
           stmt->source_site>graph->site_count) {
            minias_set_error(as,"invalid-forced-branch-source-site");
            return false;
        }
        if(!minias_query_target_relax_plan(as,stmt,&plan))return false;
        if(i<graph->forced_branch_count) {
            if(!source_graph_forced_shape_matches(&graph->forced_branches[i],
                                                  stmt)) {
                ++graph->ordinal_drifts;
                minias_set_error(as,"changed-forced-branch-order:line=%zu",
                                 stmt->line);
                return false;
            }
            if(plan.required_size>stmt->size &&
               graph->forced_branches[i].minimum_size<plan.terminal_size)
                graph->forced_branches[i].minimum_size=plan.terminal_size;
            continue;
        }
        if(plan.required_size<=stmt->size) {
            minias_set_error(as,"invalid-forced-source-growth:line=%zu",
                             stmt->line);
            return false;
        }
        if(graph->sites[stmt->source_site-1U].forced_branch_index!=0U) {
            ++graph->ordinal_drifts;
            minias_set_error(as,"ambiguous-forced-branch-source-site:line=%zu",
                             stmt->line);
            return false;
        }
        saved=&graph->forced_branches[graph->forced_branch_count];
        memset(saved,0,sizeof(*saved));
        saved->source_site=stmt->source_site;
        saved->op=minias_strdup(stmt->op);
        saved->opcode=stmt->mcinst->opcode;
        saved->target_form=stmt->mcinst->target_form;
        saved->mode_bits=stmt->site.mode_bits;
        /* Required is a lower bound; terminal is the actual Target-owned
         * alternative size committed by the MCLayout growth operation. */
        saved->minimum_size=plan.terminal_size;
        saved->operand_count=stmt->mcinst->count;
        if(saved->operand_count!=0U) {
            saved->operands=calloc(saved->operand_count,
                                   sizeof(*saved->operands));
        }
        if(saved->op==NULL ||
           (saved->operand_count!=0U && saved->operands==NULL)) {
            free(saved->op);free(saved->operands);
            memset(saved,0,sizeof(*saved));
            minias_set_error(as,"out-of-memory:forced-branch-shape");
            return false;
        }
        for(j=0U;j<saved->operand_count;++j) {
            const MiniAsMCOperand *operand=&stmt->mcinst->operands[j];
            MiniAsForcedOperandShape *shape=&saved->operands[j];
            shape->kind=operand->kind;
            shape->reg_class=operand->reg_class;
            shape->target_reg_bank=operand->target_reg_bank;
            shape->reg=operand->reg;
            shape->register_64=operand->register_64;
        }
        graph->sites[stmt->source_site-1U].forced_branch_index=
            ++graph->forced_branch_count;
    }
    return true;
}

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
    if (!minias_grow_array((void **)&as->macros,
                    &as->macro_capacity,
                    sizeof(*as->macros),
                    as->macro_count + 1U)) {
        minias_set_error(as, "out-of-memory:macro");
        free(copy);
        return false;
    }
    macro = &as->macros[as->macro_count];
    memset(macro, 0, sizeof(*macro));
    macro->definition_site=as->current_source_site;
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
        macro->body_source_segments=calloc(count,
                              sizeof(*macro->body_source_segments));
        if (macro->body == NULL || macro->body_lines == NULL ||
            macro->body_source_segments == NULL) {
            minias_set_error(as, "out-of-memory:macro-body");
            free(copy);
            return false;
        }
    }
    macro->body_count = count;
    for (i = 0U; i < count; ++i) {
        macro->body[i] = minias_strdup(lines[i].text);
        macro->body_lines[i] = lines[i].line;
        macro->body_source_segments[i]=lines[i].source_segment;
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
static bool process_inline_repeat_line(MiniAs *as,const char *source,
                                       size_t line,bool *handled);

/* Collector entry is the same for physical FILE frames and expansion views.
 * Its caller owns frame storage; the recursive collector may borrow a slice
 * for nested macro and repetition bodies without making source copies. */
static bool process_source_frame(MiniAs *as,
                                 const MiniAsSourceFrame *frame) {
    MiniAsStableSiteId previous_expansion=as->current_expansion_site;
    MiniAsStableSiteId previous_site=as->current_source_site;
    bool ok;
    if(frame==NULL ||
       (frame->kind!=MINIAS_SOURCE_FRAME_FILE &&
        frame->kind!=MINIAS_SOURCE_FRAME_EXPANSION) ||
       (frame->count!=0U && frame->lines==NULL)) {
        minias_set_error(as,"invalid-source-frame");
        return false;
    }
    if(frame->kind==MINIAS_SOURCE_FRAME_EXPANSION) {
        const MiniAsSourceSiteKey key={
            .parent=frame->invocation_site,
            .definition=frame->definition_site,
            .iteration=frame->iteration,
            .kind=frame->expansion_kind
        };
        if(frame->invocation_site==MINIAS_SITE_NONE ||
           (frame->expansion_kind!=MINIAS_SOURCE_SITE_MACRO &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_REPT &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_IRP &&
            frame->expansion_kind!=MINIAS_SOURCE_SITE_INLINE_REPEAT)) {
            minias_set_error(as,"invalid-source-expansion-site");
            return false;
        }
        if(!source_graph_intern(as,key,&as->current_expansion_site))
            return false;
    }else {
        as->current_expansion_site=MINIAS_SITE_NONE;
    }
    ok=process_source_range(as,frame->lines,0U,frame->count);
    as->current_expansion_site=previous_expansion;
    as->current_source_site=previous_site;
    return ok;
}

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
               !minias_evaluate_absolute_expression(as,expression,&value)) {
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
        expanded[i].source_segment=macro->body_source_segments[i];
    }
    ++as->macro_expansion_depth;
    {
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=expanded,
            .count=body_count,
            .expansion_kind=MINIAS_SOURCE_SITE_MACRO,
            .invocation_site=as->current_source_site,
            .definition_site=macro->definition_site
        };
        ok=process_source_frame(as,&frame);
    }
    --as->macro_expansion_depth;

done:
    destroy_source_lines(expanded, body_count);
    free(vararg_buffer);
    free(copy);
    return ok;
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
    MiniAsStableSiteId source_site=as->current_source_site;
    size_t segment=0U;
    bool split;

    if (copy == NULL) {
        minias_set_error(as, "out-of-memory:source-line");
        return false;
    }
    strip_comment(copy, as->target);
    split=find_statement_separator(copy)!=NULL;
    cursor = copy;
    while (cursor != NULL && !as->source_ended) {
        char *semicolon = find_statement_separator(cursor);
        if(split) {
            const MiniAsSourceSiteKey key={
                .parent=source_site,
                .iteration=segment,
                .kind=MINIAS_SOURCE_SITE_SEGMENT
            };
            if(!source_graph_intern(as,key,&as->current_source_site)) {
                free(copy);
                as->current_source_site=source_site;
                return false;
            }
        }
        if (semicolon != NULL) {
            *semicolon = '\0';
        }
        if (!process_statement(as, cursor, line)) {
            free(copy);
            as->current_source_site=source_site;
            return false;
        }
        cursor = semicolon == NULL ? NULL : semicolon + 1;
        ++segment;
    }
    free(copy);
    as->current_source_site=source_site;
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
         minias_evaluate_absolute_expression(as, rewritten, &value) &&
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
                              size_t line,
                              MiniAsStableSiteId invocation_site) {
    char *copy = minias_strdup(argument);
    char *comma;
    char *name;
    char *cursor;
    size_t iteration=0U;

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
    for (; !as->source_ended;) {
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
            expanded[i].source_segment=lines[i].source_segment;
            expanded[i].text =
                substitute_irp_parameter(as, lines[i].text, name, value);
            if (expanded[i].text == NULL) {
                destroy_source_lines(expanded, count);
                free(copy);
                return false;
            }
        }
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=expanded,
            .count=count,
            .expansion_kind=MINIAS_SOURCE_SITE_IRP,
            .invocation_site=invocation_site,
            .definition_site=invocation_site,
            .iteration=iteration
        };
        if (!process_source_frame(as,&frame)) {
            destroy_source_lines(expanded, count);
            free(copy);
            return false;
        }
        destroy_source_lines(expanded, count);
        ++iteration;
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

    while (i < end && !as->source_ended) {
        MiniAsStableSiteId line_site;
        bool inline_handled=false;
        char macro_argument[1024];
        int macro_kind =
            classify_macro_line(as, lines[i].text,
                                macro_argument,
                                sizeof(macro_argument));
        char argument[256];
        size_t repeat_label_prefix=0U;
        int kind;

        if(!source_graph_line_site(as,&lines[i],0U,&line_site))return false;
        as->current_source_site=line_site;

        /* File and expansion frames must agree on the semicolon-separated
         * GNU inline `.rept/.irp ... .endr` grammar. Dispatch that one
         * lexical special case before collector classification in BOTH. */
        if(!process_inline_repeat_line(as,lines[i].text,lines[i].line,
                                       &inline_handled))return false;
        if(inline_handled) {++i;continue;}

        /* GAS does not lex .macro/.rept/.irp at all in an inactive .if
         * branch. Only nested conditionals can reactivate parsing. In
         * particular, an unterminated .macro and unmatched .endm/.endr in
         * skipped text are harmless source, not collector syntax. */
        if(!as->conditional_active) {
            if(!process_source_line(as,lines[i].text,lines[i].line))return false;
            ++i;
            continue;
        }

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
                              lines[i].line))return false;
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
                for (iteration = 0U;
                     iteration < repetitions && !as->source_ended;
                     ++iteration) {
                    const MiniAsSourceFrame body={
                        .kind=MINIAS_SOURCE_FRAME_EXPANSION,
                        .lines=lines+i+1U,
                        .count=j-i-1U,
                        .expansion_kind=MINIAS_SOURCE_SITE_REPT,
                        .invocation_site=line_site,
                        .definition_site=line_site,
                        .iteration=(size_t)iteration
                    };
                    if (!process_source_frame(as,&body)) {
                        return false;
                    }
                }
            } else if (!process_irp_block(as,
                                          lines + i + 1U,
                                          j - i - 1U,
                                          argument,
                                          lines[i].line,
                                          line_site)) {
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
        if(kind<0 || !minias_grow_array((void **)&pieces,&capacity,
                                   sizeof(*pieces),count+1U)) {
            minias_set_error(as,"out-of-memory:inline-repeat-segments");
            ok=false;break;
        }
        pieces[count].text=cursor;
        pieces[count].line=line;
        pieces[count].source_segment=count;
        ++count;
        if(kind==MINIAS_REPEAT_REPT || kind==MINIAS_REPEAT_IRP)opener=true;
        if(kind==MINIAS_REPEAT_ENDR)closer=true;
        cursor=end==NULL?NULL:end+1U;
    }
    if(ok && opener && closer) {
        const MiniAsSourceFrame frame={
            .kind=MINIAS_SOURCE_FRAME_EXPANSION,
            .lines=pieces,
            .count=count,
            .expansion_kind=MINIAS_SOURCE_SITE_INLINE_REPEAT,
            .invocation_site=as->current_source_site,
            .definition_site=as->current_source_site
        };
        *handled=true;
        ok=process_source_frame(as,&frame);
    }
    for(i=0U;i<count;++i)pieces[i].text=NULL; /* owned by copy */
    free(pieces);
    free(copy);
    return ok;
}

/* Source text may contain literal binary bytes inside quoted .ascii data,
 * and .incbin files are never inspected as assembly text.  But the private
 * SymbolID sigil is forbidden outside quoted strings, even in macro/rept
 * bodies; only the internal numeric/target rewriter may create one. */
static bool validate_user_source_private_marker(MiniAs *as,
                                                const char *line,
                                                size_t source_line) {
    char quote='\0';
    bool escaped=false;
    const unsigned char *p=(const unsigned char *)line;
    for(;*p!='\0';++p) {
        if(escaped) {escaped=false;continue;}
        if(quote!='\0') {
            if(*p=='\\')escaped=true;
            else if(*p==(unsigned char)quote)quote='\0';
        }else if(*p=='"' || *p=='\'') {
            quote=(char)*p;
        }else if(*p==1U) {
            minias_set_error(as,"private-symbol-marker-in-source:line=%zu",
                             source_line);
            return false;
        }
    }
    return true;
}

/* A physical input is lexed exactly once into an owned SourceFrame.  The
 * shared range collector below now handles FILE, expanded MACRO and
 * REPT/IRP bodies, including nested collectors and inactive .if branches.
 * The fully retained frame avoids independent streaming collector paths
 * whose treatment of inline semicolons and nested directives could diverge.
 *
 * The assembly session retains the normalized physical frame across rare
 * layout-sensitive source replays. Expanded macro/rept/irp bodies still
 * have their original per-parse lifetime and GNU lexical counters. */
static bool collect_file_source_frame(MiniAs *as, const char *path,
                                      MiniAsSourceFrame *frame) {
    FILE *file=fopen(path,"r");
    char linebuf[65536];
    size_t line_no=0U;
    bool ok=true;
    if(file==NULL) {
        minias_set_error(as,"input-open:%s",path);
        return false;
    }
    as->c_comment_active=false;
    frame->kind=MINIAS_SOURCE_FRAME_FILE;
    while(fgets(linebuf,sizeof(linebuf),file)!=NULL) {
        size_t len;
        MiniAsSourceLine *line;
        ++line_no;
        if(!validate_user_source_private_marker(as,linebuf,line_no)) {
            ok=false;break;
        }
        strip_gnu_c_comments(as,linebuf);
        len=strlen(linebuf);
        if(len==sizeof(linebuf)-1U && linebuf[len-1U]!='\n') {
            minias_set_error(as,"line-too-long:line=%zu",line_no);
            ok=false;break;
        }
        if(!minias_grow_array((void **)&frame->lines,
                              &frame->capacity,sizeof(*frame->lines),
                              frame->count+1U)) {
            minias_set_error(as,"out-of-memory:source-frame");
            ok=false;break;
        }
        line=&frame->lines[frame->count];
        line->text=minias_strdup(linebuf);
        if(line->text==NULL) {
            minias_set_error(as,"out-of-memory:source-frame-line");
            ok=false;break;
        }
        line->line=line_no;
        line->source_segment=0U;
        ++frame->count;
    }
    if(ok && ferror(file)) {
        minias_set_error(as,"input-read:%s",path);
        ok=false;
    }
    fclose(file);
    if(ok && as->c_comment_active) {
        minias_set_error(as,"unterminated-gnu-block-comment");
        ok=false;
    }
    return ok;
}

static bool parse_loaded_source_graph(MiniAs *as,
                                       MiniAsSourceGraph *graph) {
    MiniAsSourceLine eof={0};
    bool parsed;
    size_t i;
    if(as->source_graph!=NULL || graph==NULL ||
       graph->physical.kind!=MINIAS_SOURCE_FRAME_FILE) {
        minias_set_error(as,"invalid-source-graph-state");
        return false;
    }
    as->source_graph=graph;
    ++graph->parse_passes;
    parsed=process_source_frame(as,&graph->physical);
    if(!parsed)return false;
    if(as->conditional_count!=0U) {
        minias_set_error(as,"unterminated-directive:.if:depth=%zu",
                         as->conditional_count);
        return false;
    }
    if(as->cfi_active) {
        minias_set_error(as,"unterminated-directive:.cfi_startproc");
        return false;
    }
    /* EOF-scheduled literal pools are generated by a real synthetic source
     * site, just like explicit .ltorg children are owned by their opener. */
    eof.line=graph->physical.count+1U;
    if(!source_graph_line_site(as,&eof,0U,&as->current_source_site))
        return false;
    if(as->target->supports_literal_pools &&
       !flush_literal_pools(as,graph->physical.count,true))return false;
    /* Every emitted Fragment, including EOF literal-pool children, must
     * retain an actual source/expansion owner before SourceGraph is used
     * for cross-replay ordinal diagnostics. */
    for(i=0U;i<as->stmt_count;++i)
        if(as->stmts[i].source_site==MINIAS_SITE_NONE ||
           as->stmts[i].source_site>graph->site_count) {
            minias_set_error(as,"fragment-without-source-site:%zu",i);
            return false;
        }
    if(!minias_finalize_subsection_layout(as))return false;
    return minias_refresh_symbol_aliases(as);
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
    /* Equal final offsets occur at subsection boundaries. GNU emits all
     * zero-size origin/label fragments in subsection 0 BEFORE instructions
     * in subsection 1 even if the latter appeared earlier in source. A
     * source-order tiebreak incorrectly wrote a subsection-1 .inst first,
     * making the preceding .org appear to overlap its four bytes (real
     * Linux arm64 KVM hyp-entry). Order by output subsection ownership. */
    if (lhs->stmt->subsection != rhs->stmt->subsection) {
        return lhs->stmt->subsection < rhs->stmt->subsection ? -1 : 1;
    }
    if (lhs->order != rhs->order) {
        return lhs->order < rhs->order ? -1 : 1;
    }
    return 0;
}

bool minias_emit_sections(MiniAs *as) {
    size_t i;
    MiniAsEmitRef *refs = NULL;

    if(!minias_validate_fragment_semantics(as))return false;
    if (as->active_encode_result != NULL) {
        minias_set_error(as, "encode-result-reentered");
        return false;
    }
    as->active_encode_result = calloc(1U, sizeof(*as->active_encode_result));
    if (as->active_encode_result == NULL) {
        minias_set_error(as, "out-of-memory:encode-result");
        return false;
    }

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
        /* The engine owns the complete fragment transaction. No direct
         * DATA/ALIGN/FILL append path survives inside source emission. */
        as->emitting_fragment = true;
        as->emitting_fragment_id = refs[i].order;
        as->committing_fragment=false;
        if (stmt->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT ||
            stmt->fragment_kind == MINIAS_FRAGMENT_ORIGIN) {
            bool exec_align = (section->flags & MINIAS_SHF_EXECINSTR) != 0U &&
                              stmt->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT;
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
        } else if (stmt->fragment_kind == MINIAS_FRAGMENT_FIXED_DATA ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_FILL ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_SPACE ||
                   stmt->fragment_kind == MINIAS_FRAGMENT_BYTES) {
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
        /* Every source Fragment uses the same measured-size validation
         * and commit. No fixed DATA, ALIGN, FILL or SPACE bypass remains. */
        if(as->active_encode_result->byte_count!=stmt->size ||
           stmt->offset>SIZE_MAX) {
            minias_set_error(as,
                "fragment-emitted-size-mismatch:line=%zu:expected=%u:actual=%zu",
                stmt->line,stmt->size,
                as->active_encode_result->byte_count);
            free(refs);
            return false;
        }
        if(!minias_commit_encode_result(as,stmt)) {
            free(refs);
            return false;
        }
        as->emitting_fragment = false;
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
    size_t source_reparse_pass = 0U;
    MiniAsSourceGraph graph={0};
    bool source_loaded=false;
    int rc = 1;

    for (;;) {
        MiniAs as;
        bool parsed;
        bool added_long_branches = false;

        const bool initialized=minias_init_for_target(&as,config->target);
        as.elf_flags = config->elf_flags;
        as.output_elf32 = config->elf32;
        as.object_machine = config->object_machine;
        as.output_rel_relocations = config->output_rel_relocations;
        if (config->target_profile_size > sizeof(config->target_profile)) {
            minias_set_error(&as, "invalid-target-profile-size");
        } else if (initialized && as.target->init_target_state != NULL &&
                   !as.target->init_target_state(&as, config) &&
                   as.error[0] == '\0') {
            minias_set_error(&as, "target-initialization-failed");
        }
        parsed=initialized && as.error[0]=='\0';
        if(parsed && as.target->default_exec_alignment_for_site!=NULL) {
            /* Initial .text is created before target ISA state exists.
             * Reconcile its default after profile initialization, before
             * any source fragment can rely on the Section alignment. */
            int text_section=minias_find_section(&as,".text");
            uint64_t align=minias_target_exec_alignment(&as);
            if(text_section<0 || align==0U || (align&(align-1U))!=0U) {
                minias_set_error(&as,"invalid-target-exec-alignment");
                parsed=false;
            } else {
                as.sections[(size_t)text_section].align=align;
            }
        }
        if(parsed && !source_loaded) {
            parsed=collect_file_source_frame(&as,input_path,&graph.physical);
            if(parsed) {
                source_loaded=true;
                ++graph.file_reads;
            }
        }
        if(parsed)parsed=parse_loaded_source_graph(&as,&graph);
        if(parsed)parsed=source_graph_rebind_forced_branches(
                &as,&long_branch_ids,&long_branch_count,
                &long_branch_capacity);
        if(parsed && !minias_solve_fragment_layout(
              &as,&long_branch_ids,&long_branch_count,
              &long_branch_capacity,&added_long_branches,diagnostic))
            parsed=false;
        if(parsed && added_long_branches) {
            parsed=source_graph_capture_forced_branches(&as,long_branch_ids,
                                                        long_branch_count);
        }
        if(parsed && added_long_branches) {
            ++source_reparse_pass;
            /* Even adversarial conditional/macro feedback cannot keep
             * rebuilding whole semantic states without a finite bound. */
            if(source_reparse_pass>1024U) {
                minias_set_error(&as,"source-replay-nonconvergent");
                parsed=false;
            }
        }
        if(parsed && added_long_branches) {
            /* The successful pass prints reparse=0 because it is an in-place
             * layout trace.  Report the actual exceptional source replay
             * here, without changing the protected .if/.set semantics. */
            if (diagnostic != NULL && getenv("MINIAS_TRACE_RELAYOUT") != NULL)
                fprintf(diagnostic,"minias-source-reparse:pass=%zu\n",
                        source_reparse_pass);
            minias_destroy(&as);
            continue;
        }
        if (parsed && as.error[0] == '\0' &&
            minias_emit_sections(&as) &&
            (as.target->validate_output == NULL ||
             as.target->validate_output(&as)) &&
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
    if(diagnostic!=NULL && getenv("MINIAS_TRACE_RELAYOUT")!=NULL)
        fprintf(diagnostic,
                "minias-source-graph:file-reads=%zu sites=%zu expansions=%zu replay-passes=%zu site-drifts=%zu conditional-changes=%zu\n",
                graph.file_reads,graph.site_count,graph.expansion_count,
                source_reparse_pass,graph.ordinal_drifts,
                graph.conditional_changes);
    source_graph_destroy(&graph);
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
    if (strict_i_xlen != 0U &&
        (target->configure_legacy_target == NULL ||
         !target->configure_legacy_target(&config, strict_i_xlen))) {
        if (diagnostic != NULL)
            fprintf(diagnostic, "minic-as: unsupported-legacy-target-configuration\n");
        return 1;
    }
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
