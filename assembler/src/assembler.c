#include "minias_internal.h"
#include "source_graph_internal.h"
#include "minias.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
    /* SHN_ABS aliases are integer expressions, not section offsets.
     * GNU GAS keeps their full 64-bit two's-complement bit pattern: a
     * forward `zero-2` is -2 and even UINT64_MAX+1 wraps to zero.  The
     * nonnegative address/overflow rule below applies only to symbols
     * owned by real sections, where an underflow cannot be an address. */
    if (target->section == MINIAS_SECTION_ABS) {
        value += (uint64_t)alias->alias_addend;
    } else if (alias->alias_addend >= 0) {
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

/* SectionID is the identity of one output Section throughout parse, layout
 * and ELF serialization. GNU can create several Sections with the same
 * spelling; group signature, LINK_ORDER source anchor and explicit unique ID
 * all distinguish them. The index is immutable across Section array moves.
 * Absence of unique is different from the valid numeric ID zero. */
typedef struct MiniAsSectionIdentity {
    const char *name;
    const char *group_signature;
    const char *link_order_reference;
    bool has_unique_id;
    uint32_t unique_id;
} MiniAsSectionIdentity;

static MiniAsSectionIdentity section_identity(const MiniAsSection *section) {
    MiniAsSectionIdentity result={section->name,section->group_signature,
                                  section->link_order_reference,
                                  section->has_unique_id,section->unique_id};
    return result;
}

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

static size_t section_key_hash(const MiniAsSectionIdentity *key) {
    uint64_t hash=UINT64_C(1469598103934665603);
    uint32_t id=key->unique_id;
    size_t i;
    hash=section_key_hash_component(hash,key->name);
    hash=section_key_hash_component(hash,key->group_signature);
    hash=section_key_hash_component(hash,key->link_order_reference);
    hash^=key->has_unique_id ? UINT64_C(1) : UINT64_C(0);
    hash*=UINT64_C(1099511628211);
    for(i=0U;i<sizeof(id);++i) {
        hash^=(id>>(i*8U))&UINT32_C(0xff);
        hash*=UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool section_key_component_equal(const char *a,const char *b) {
    return a==b || (a!=NULL && b!=NULL && strcmp(a,b)==0);
}

static bool section_key_equal(const MiniAsSection *sec,
                              const MiniAsSectionIdentity *key) {
    return section_key_component_equal(sec->name,key->name) &&
           section_key_component_equal(sec->group_signature,key->group_signature) &&
           section_key_component_equal(sec->link_order_reference,key->link_order_reference) &&
           sec->has_unique_id==key->has_unique_id &&
           (!key->has_unique_id || sec->unique_id==key->unique_id);
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
        const MiniAsSectionIdentity key=section_identity(sec);
        size_t slot=section_key_hash(&key)&(capacity-1U);
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

static size_t minias_section_index_slot(const MiniAs *as,
                                        const MiniAsSectionIdentity *key) {
    size_t slot=section_key_hash(key)&
                (as->section_slot_capacity-1U);
    /* At most half of the table is occupied. An empty slot always exists. */
    while(as->section_slots[slot]!=0U) {
        const size_t section_id=as->section_slots[slot]-1U;
        if(section_key_equal(&as->sections[section_id],key))
            break;
        slot=(slot+1U)&(as->section_slot_capacity-1U);
    }
    return slot;
}

static int minias_find_section_key(const MiniAs *as,
                                   const MiniAsSectionIdentity *key) {
    size_t slot;
    if(as->section_slot_capacity==0U)return -1;
    slot=minias_section_index_slot(as,key);
    return as->section_slots[slot]==0U ? -1 :
           (int)(as->section_slots[slot]-1U);
}

int minias_find_section(const MiniAs *as,const char *name) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_find_section_key(as,&key);
}

static int minias_ensure_section_key(MiniAs *as,
                                     const MiniAsSectionIdentity *key,
                                     uint32_t group_flags,
                                     uint32_t type,uint64_t flags,
                                     uint64_t align) {
    const int found=minias_find_section_key(as,key);
    MiniAsSection *sec;
    size_t slot;
    if(found>=0) {
        sec=&as->sections[(size_t)found];
        if(sec->group_flags!=group_flags) {
            minias_set_error(as,"conflicting-section-group-linkage:%s",key->name);
            return -1;
        }
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
    sec->name=minias_strdup(key->name);
    if(sec->name==NULL) {
        minias_set_error(as,"out-of-memory:section-name");
        return -1;
    }
    if(key->group_signature!=NULL) {
        sec->group_signature=minias_strdup(key->group_signature);
        if(sec->group_signature==NULL) {
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:group-signature");
            return -1;
        }
    }
    if(key->link_order_reference!=NULL) {
        sec->link_order_reference=minias_strdup(key->link_order_reference);
        if(sec->link_order_reference==NULL) {
            free(sec->group_signature);sec->group_signature=NULL;
            free(sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:link-order-reference");
            return -1;
        }
    }
    sec->type=type;
    sec->flags=flags;
    sec->group_flags=group_flags;
    sec->has_unique_id=key->has_unique_id;
    sec->unique_id=key->unique_id;
    sec->align=align==0U ? 1U : align;
    /* No failure point remains: publish SectionID and hash entry together. */
    slot=minias_section_index_slot(as,key);
    as->section_slots[slot]=as->section_count+1U;
    ++as->section_count;
    return (int)(as->section_count-1U);
}

int minias_ensure_section(MiniAs *as,
                          const char *name,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t align) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_ensure_section_key(as,&key,0U,type,flags,align);
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
static bool minias_fixup_field_width(MiniAsFixupKind kind,unsigned width) {
    if(kind==MINIAS_FIXUP_LEB_ADD || kind==MINIAS_FIXUP_LEB_SUB ||
       kind==MINIAS_FIXUP_LEB_SET)return width==0U;
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
                      MiniAsFixupOrigin origin,bool absolute_zero_symbol,
                      size_t retained_fragment_id) {
    MiniAsSymbol *symbol=NULL;
    MiniAsFixup *fixup;
    size_t symbol_index;
    uint32_t relative_offset = 0U;
    size_t fragment_id = SIZE_MAX;
    if (origin == MINIAS_FIXUP_ORIGIN_FRAGMENT) {
        const MiniAsStmt *owner;
        uint64_t delta;
        if ((as->emitting_fragment && retained_fragment_id!=SIZE_MAX) ||
            (!as->emitting_fragment && retained_fragment_id==SIZE_MAX)) {
            minias_set_error(as, "fixup-without-fragment-owner");
            return false;
        }
        fragment_id = retained_fragment_id!=SIZE_MAX ? retained_fragment_id :
                     as->emitting_fragment_id;
        if(fragment_id>=as->stmt_count) {
            minias_set_error(as,"invalid-retained-fixup-fragment-id");
            return false;
        }
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
        !minias_fixup_field_width(kind,width) ||
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
       offset<owner->offset || !minias_fixup_field_width(kind,width) ||
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
    return minias_stage_common_fixup(as,section,offset,kind,width,
                                     symbol_name,SIZE_MAX,addend);
}

/* Fixed-width MCData already retains an actual SymbolID from its lexical
 * parse. Preserve that identity through emission, including the shared
 * undefined-local binding promotion and original Fixup append order. */
bool minias_add_fixup_symbol_id(MiniAs *as, int section, uint64_t offset,
                                MiniAsFixupKind kind, unsigned width,
                                size_t symbol_index, int64_t addend) {
    return minias_stage_common_fixup(as,section,offset,kind,width,
                                     NULL,symbol_index,addend);
}

bool minias_add_section_fixup(MiniAs *as, MiniAsFixupOrigin origin,
                             int section, uint64_t offset,
                             MiniAsFixupKind kind, unsigned width,
                             const char *symbol_name, int64_t addend) {
    return minias_record_fixup(as, section, offset, kind, width,
                               symbol_name, SIZE_MAX, addend, origin,false,
                               SIZE_MAX);
}

/* The final linker-relaxation predicate needs all Target MCInst fixups,
 * including instructions parsed *after* the ULEB source line. A second
 * retained-fragment commit window allows its paired relocation records to
 * be added after full emission without pretending they are .reloc or DWARF
 * section-owned fixups. The encoded ULEB bytes remain the fragment's own. */
bool minias_add_late_fragment_fixup(MiniAs *as,size_t fragment_id,
                                   MiniAsFixupKind kind,size_t symbol_index,
                                   int64_t addend) {
    const MiniAsStmt *stmt;
    if(as==NULL || as->emitting_fragment || as->committing_fragment ||
       fragment_id>=as->stmt_count || symbol_index>=as->symbol_count) {
        if(as!=NULL)minias_set_error(as,"invalid-late-fragment-fixup");
        return false;
    }
    stmt=&as->stmts[fragment_id];
    if(stmt->fragment_kind!=MINIAS_FRAGMENT_BYTES || stmt->leb_mode!=1U ||
       (kind!=MINIAS_FIXUP_LEB_ADD && kind!=MINIAS_FIXUP_LEB_SUB &&
        kind!=MINIAS_FIXUP_LEB_SET)) {
        minias_set_error(as,"late-fixup-non-uleb-fragment:line=%zu",
                         stmt->line);
        return false;
    }
    if(!minias_record_fixup(as,stmt->section,stmt->offset,kind,0U,
                            NULL,symbol_index,addend,
                            MINIAS_FIXUP_ORIGIN_FRAGMENT,false,fragment_id))
        return false;
    /* GNU GAS local `.L` label differences are serialized via their
     * canonical STT_SECTION and the label's final section-relative addend. */
    as->fixups[as->fixup_count-1U].prefer_section_symbol=true;
    return true;
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
                                 request->absolute_zero_symbol,SIZE_MAX))
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

/* Target-owned executable alignment NOP bytes and the private fragment
 * transaction must use one policy. A fill like x86's 0x90 can explicitly
 * request canonical NOPs while other fill bytes remain literal DATA. */
static bool minias_alignment_emits_target_nops(const MiniAs *as,
                                               const MiniAsStmt *stmt,
                                               const MiniAsSection *sec) {
    return stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT &&
           (sec->flags & MINIAS_SHF_EXECINSTR)!=0U &&
           (!stmt->align_has_fill ||
            (as->target->explicit_alignment_fill_is_code_nop &&
             as->target->explicit_alignment_fill_is_code_nop(
                 stmt->align_fill)));
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
         !minias_alignment_emits_target_nops(as,owner,sec)) ||
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
         !minias_alignment_emits_target_nops(as,owner,sec)) :
        (owner->fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
         minias_alignment_emits_target_nops(as,owner,sec))) ||
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
       minias_alignment_emits_target_nops(as,owner,sec) ||
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
    if(!minias_source_context_init(as))return false;
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
    as->debug_is_stmt=true;
    return minias_ensure_subsection(as,text,0U)!=NULL;
}

static uint64_t minias_target_exec_alignment(const MiniAs *as) {
    unsigned align=as->target->default_exec_alignment_for_site!=NULL ?
        as->target->default_exec_alignment_for_site(as) : 0U;
    if(align==0U)align=as->target->default_exec_alignment;
    return align!=0U ? align : 4U;
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
    free(as->pending_local_commons);
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
    minias_source_context_destroy(as->source);
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
    st->source_site = as->source->current_site;
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
    /* Code-after-data needs a GNU target-defined implicit alignment. Record
     * the transition per subsection so enormous instruction-only Linux
     * regions do not grow a redundant alignment Fragment per instruction.
     * The actual padding is deferred to MCLayout, where symbolic LEBs and
     * other variable-width preceding data have their final sizes. */
    if(fragment_kind==MINIAS_FRAGMENT_INSTRUCTION ||
       fragment_kind==MINIAS_FRAGMENT_FIXED_DATA ||
       fragment_kind==MINIAS_FRAGMENT_BYTES ||
       fragment_kind==MINIAS_FRAGMENT_FILL ||
       fragment_kind==MINIAS_FRAGMENT_SPACE ||
       fragment_kind==MINIAS_FRAGMENT_ORIGIN) {
        MiniAsSubsection *sub=minias_ensure_subsection(
            as,st->section,st->subsection);
        if(sub==NULL)return false;
        sub->pending_instruction_alignment=
            fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION;
    }
    return true;
}

/* GNU AArch64 emits zero data padding before an instruction after an odd-
 * sized .byte/.uleb128, while regular .data instructions are not padded.
 * This is a target-advertised alignment capability, independent of ELF
 * section alignment and shared for real instructions and .inst words.
 * Retain an ordinary ALIGN Fragment so symbolic LEB resizing remains in
 * MCLayout's one fixed point (including anchors before the next opcode). */
static bool minias_maybe_align_instruction(MiniAs *as,size_t line) {
    const unsigned alignment=as->target->implicit_instruction_alignment;
    MiniAsSubsection *sub;
    MiniAsStmt *stmt;
    uint64_t pad;
    if(alignment==0U ||
       (as->sections[(size_t)as->current_section].flags&MINIAS_SHF_EXECINSTR)==0U)
        return true;
    if((alignment&(alignment-1U))!=0U || alignment>UINT32_MAX) {
        minias_set_error(as,"invalid-target-implicit-code-alignment");
        return false;
    }
    sub=minias_ensure_subsection(as,as->current_section,as->current_subsection);
    if(sub==NULL)return false;
    pad=(alignment-(sub->size&(alignment-1U)))&(alignment-1U);
    if(!sub->pending_instruction_alignment && pad==0U)return true;
    if(!add_stmt(as,MINIAS_FRAGMENT_ALIGNMENT,".implicit_code_align","",
                 line,(uint32_t)pad,alignment))return false;
    stmt=&as->stmts[as->stmt_count-1U];
    /* GNU inserts zero bytes here, not 4-byte NOP instructions. Mark this
     * as explicit DATA fill to prevent the target NOP alignment callback. */
    stmt->align_has_fill=true;
    stmt->align_fill=0U;
    if(!advance_current_location(as,pad))return false;
    sub->pending_instruction_alignment=false;
    if(as->sections[(size_t)as->current_section].align<alignment)
        as->sections[(size_t)as->current_section].align=alignment;
    return true;
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
                           bool has_unique_id,
                           uint32_t unique_id,
                           uint32_t group_flags,
                           uint32_t type,
                           uint64_t flags,
                           uint64_t align) {
    const MiniAsSectionIdentity key={name,group_signature,
                                      link_order_reference,
                                      has_unique_id,unique_id};
    int index = minias_ensure_section_key(as,&key,group_flags,
                                          type,flags,align);

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
    uint32_t group_flags = 0U;
    uint64_t entry_size=0U;
    bool has_unique_id=false,inherit_group=false;
    uint32_t unique_id=0U;
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
                } else if (*q == '?') {
                    inherit_group=true;
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
            char *fields[8];
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
                if(next<field_count &&
                   !(strcmp(fields[next],"unique")==0 &&
                     next+1U<field_count)) {
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
                const char *sig;
                if(next>=field_count) {
                    minias_set_error(as,"missing-group-signature:%s:line=%zu",name,line);
                    return false;
                }
                sig=fields[next++];
                if(*sig=='\0' ||
                   strlen(sig)>=256U ||
                   strpbrk(sig," \t,\r\n\"")!=NULL) {
                    minias_set_error(as,"bad-section-group:%s:line=%zu",name,line);
                    return false;
                }
                group_signature=sig;
                if(next<field_count &&
                   (strcmp(fields[next],"comdat")==0 ||
                    strcmp(fields[next],".gnu.linkonce")==0)) {
                    group_flags=MINIAS_GRP_COMDAT;
                    ++next;
                }
            }
            /* GAS treats this trailing pair as ELF Section identity, not
             * another section attribute. ID zero must stay distinguishable
             * from an ordinary section with no unique argument. */
            if(next<field_count && strcmp(fields[next],"unique")==0) {
                uint64_t parsed;
                ++next;
                if(next>=field_count ||
                   !minias_parse_u64(fields[next++],&parsed) ||
                   parsed>UINT32_MAX) {
                    minias_set_error(as,"bad-section-unique-id:%s:line=%zu",name,line);
                    return false;
                }
                has_unique_id=true;
                unique_id=(uint32_t)parsed;
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

    if(inherit_group && (flags & MINIAS_SHF_GROUP)!=0U) {
        minias_set_error(as,"conflicting-section-group-inheritance:%s:line=%zu",name,line);
        return false;
    }
    if ((flags & MINIAS_SHF_GROUP)!=0U && group_signature==NULL) {
        minias_set_error(as,"missing-section-group:%s:line=%zu",name,line);
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
    if(inherit_group && as->current_section>=0 &&
       (size_t)as->current_section<as->section_count) {
        const MiniAsSection *prior=&as->sections[(size_t)as->current_section];
        if(prior->group_signature!=NULL) {
            flags|=MINIAS_SHF_GROUP;
            group_signature=prior->group_signature;
            group_flags=prior->group_flags;
        }
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
        const MiniAsSectionIdentity key={name,group_signature,
                                          link_order_reference,
                                          has_unique_id,unique_id};
        int prior=minias_find_section_key(as,&key);
        bool ok=switch_section(as,name,group_signature,link_order_reference,
                has_unique_id,unique_id,group_flags,type,flags,
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
        delayed=common_data_has_symbolic_token(args);
        /* Compile the retained expression before computing its provisional
         * size below, after lexical .set snapshots have been captured. The
         * former immediate parser was a second grammar for these fields. */
        bytes=0U;
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
        delayed=common_data_has_symbolic_token(args);
        bytes=0U;
    } else {
        /* One Common MCExpr parser validates every fixed-width field.
         * Measurement only counts fields; parsed semantic expressions are
         * retained on the stable DATA Fragment for emission. */
        if(width==0U) {
            minias_set_error(as,"invalid-fixed-data-width:%s",op);
            return false;
        }
        /* GNU GAS accepts an operand-free fixed-width directive as a
         * zero-byte statement (`663: .long` appears in AArch64 Linux
         * alternative-insn expansions). An empty argument list is not a
         * single invalid MCExpr or an implicit zero-valued element. */
        count=*minias_trim(args)=='\0'?0U:1U;
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
    if(fragment_kind==MINIAS_FRAGMENT_FILL ||
       fragment_kind==MINIAS_FRAGMENT_SPACE) {
        MiniAsStmt *stmt=&as->stmts[as->stmt_count-1U];
        uint64_t repeat=0U,value=0U;
        unsigned element_width=0U;
        /* The stored spelling is already frozen at this lexical site.
         * Compile exactly once; both the first-pass placement and the
         * later MCLayout fixed point evaluate these same owned SymbolIDs. */
        if(!minias_compile_variable_exprs(as,stmt))return false;
        if(minias_resolve_variable_exprs(as,stmt,&repeat,&element_width,&value) &&
           (element_width==0U || repeat<=UINT32_MAX/element_width)) {
            bytes=repeat*element_width;
            stmt->size=(uint32_t)bytes;
            variable_parsed=true;
            variable_repeat=repeat;
            variable_width=element_width;
            variable_value=value;
        }else {
            /* Forward symbols and section-relative values may settle only
             * after relaxation. MCLayout also rejects malformed resolved
             * size/value combinations without a speculative source reparse. */
            delayed=true;
        }
    }
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
        return minias_section_append_fill(as,stmt->section,stmt->size,
                                          (uint8_t)stmt->variable_value,true);
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

bool minias_numeric_refs_rewrite(MiniAs *as,
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
        MiniAsPendingLocalCommon *pending;
        int bss;
        if((uint64_t)size>UINT32_MAX || alignment>UINT32_MAX)goto invalid;
        bss=minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U);
        if(bss<0){free(copy);return false;}
        if(!minias_grow_array((void **)&as->pending_local_commons,
                &as->pending_local_common_capacity,
                sizeof(*as->pending_local_commons),
                as->pending_local_common_count+1U)) {
            minias_set_error(as,"out-of-memory:pending-local-common");
            free(copy);return false;
        }
        pending=&as->pending_local_commons[as->pending_local_common_count++];
        pending->symbol_index=(size_t)(symbol-as->symbols);
        pending->size=(uint64_t)size;
        pending->alignment=alignment;
        pending->line=line;
        pending->source_site=as->source->current_site;
        /* Mark this SymbolID as defined immediately, preserving GNU's
         * duplicate-symbol and late .globl/.hidden semantics, but do not
         * anchor its BSS offset until the complete subsection-0 source
         * stream exists. It will become a real Fragment anchor at EOF. */
        symbol->defined=true;
        symbol->section=bss;
        symbol->subsection=0U;
        symbol->value=0U;
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

/* GAS allocates local .comm symbols after every explicit .bss subsection-0
 * declaration, even if the source's .comm appeared first. It does NOT wait
 * until numbered subsections 1+ have been laid out. Scheduling these typed
 * ALIGN/ZERO Fragments at source EOF under the same BSS subsection gives
 * the shared MCLayout solver the exact final allocation order and keeps the
 * existing SymbolID/ELF REL section-fold path unmodified. */
static bool flush_pending_local_commons(MiniAs *as) {
    int saved_section=as->current_section;
    uint32_t saved_subsection=as->current_subsection;
    MiniAsStableSiteId saved_source_site=as->source->current_site;
    bool ok=true;
    size_t i;
    int bss;
    if(as->pending_local_common_count==0U)return true;
    bss=minias_find_section(as,".bss");
    if(bss<0) {
        minias_set_error(as,"pending-local-common-missing-bss");
        return false;
    }
    as->current_section=bss;
    as->current_subsection=0U;
    if(minias_ensure_subsection(as,bss,0U)==NULL)ok=false;
    for(i=0U;ok && i<as->pending_local_common_count;++i) {
        const MiniAsPendingLocalCommon *pending=&as->pending_local_commons[i];
        MiniAsSymbol *symbol;
        uint64_t here;
        char align_buf[32],size_buf[32];
        if(pending->symbol_index>=as->symbol_count) {
            minias_set_error(as,"pending-local-common-invalid-symbol");
            ok=false;break;
        }
        symbol=&as->symbols[pending->symbol_index];
        if(!symbol->defined || symbol->section!=bss ||
           symbol->has_fragment_anchor) {
            minias_set_error(as,"pending-local-common-mutated:%s",symbol->name);
            ok=false;break;
        }
        as->source->current_site=pending->source_site;
        (void)snprintf(align_buf,sizeof(align_buf),"%llu",
                       (unsigned long long)pending->alignment);
        (void)snprintf(size_buf,sizeof(size_buf),"%llu",
                       (unsigned long long)pending->size);
        if(!handle_align(as,".balign",align_buf,pending->line) ||
           !minias_current_location(as,&here)) {
            ok=false;break;
        }
        symbol->value=here;
        symbol->subsection=0U;
        symbol->has_fragment_anchor=true;
        symbol->fragment_anchor=as->stmt_count;
        symbol->fragment_anchor_addend=0;
        if(!add_data_stmt(as,".zero",size_buf,pending->line)) {
            ok=false;break;
        }
    }
    as->current_section=saved_section;
    as->current_subsection=saved_subsection;
    as->source->current_site=saved_source_site;
    return ok;
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


bool minias_statement_process(MiniAs *as, char *text, size_t line) {
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
        return minias_source_push_conditional_value(as,nonblank?*arg!='\0':*arg=='\0');
    }
    if ((strncmp(text,".ifeqs",6U)==0 ||
         strncmp(text,".ifnes",6U)==0) &&
        (text[6]=='\0' || isspace((unsigned char)text[6]))) {
        bool unequal=text[3]=='n';
        return minias_source_push_conditional_strings(as,minias_trim(text+6U),
                                        unequal,true,line);
    }
    if ((strncmp(text,".ifc",4U)==0 ||
         strncmp(text,".ifnc",5U)==0) &&
        ((text[3]=='c' && (text[4]=='\0' ||
                          isspace((unsigned char)text[4]))) ||
         (text[3]=='n' && text[4]=='c' &&
          (text[5]=='\0' || isspace((unsigned char)text[5]))))) {
        bool unequal=text[3]=='n';
        return minias_source_push_conditional_strings(as,
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
        return minias_source_push_conditional_defined(as,minias_trim(text+width),
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
                return minias_source_push_conditional_numeric(as,
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
        return minias_source_push_conditional(as, minias_trim(text + 3), line);
    }
    if (strncmp(text, ".elseif", 7U) == 0 &&
        (text[7] == '\0' || isspace((unsigned char)text[7]))) {
        int64_t literal;
        if (!minias_eval_i64_expression(text+7U,&literal))
            as->layout_sensitive=true;
        return minias_source_elseif_conditional(as,minias_trim(text+7U),line);
    }
    if (strcmp(text, ".else") == 0) {
        return minias_source_else_conditional(as, line);
    }
    if (strcmp(text, ".endif") == 0) {
        return minias_source_pop_conditional(as, line);
    }
    if (!as->source->conditional_active) {
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
    if(had_leading_label)return minias_statement_process(as,text,line);

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
        if(!*name || strpbrk(name,", \t\r")!=NULL) {
            minias_set_error(as,"bad-directive:.purgem:%s:line=%zu",name,line);
            return false;
        }
        minias_macro_purge(as->source,name);
        return true;
    }
    {
        MiniAsMacro *macro = minias_macro_find(as, op);
        if (macro != NULL) {
            return minias_macro_expand(as, macro, args, line);
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
                macro=minias_macro_find(as,op);
                /* Registry lookup needs the temporary NUL, but GNU's
                 * actual operand starts at the ORIGINAL '(' character. */
                *open='(';
                if(macro!=NULL)
                    return minias_macro_expand(as,macro,open,line);
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
                                  false,0U,0U,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_EXECINSTR,
                                  minias_target_exec_alignment(as));
        }
        if (strcmp(op, ".data") == 0) {
            return switch_section(as,
                                  ".data",
                                  NULL,
                                  NULL,
                                  false,0U,0U,
                                  MINIAS_SHT_PROGBITS,
                                  MINIAS_SHF_ALLOC | MINIAS_SHF_WRITE,
                                  1U);
        }
        if (strcmp(op, ".bss") == 0) {
            return switch_section(as,
                                  ".bss",
                                  NULL,
                                  NULL,
                                  false,0U,0U,
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
            as->source->altmacro_active=strcmp(op,".altmacro")==0;
            return true;
        }
        if (strcmp(op, ".ident") == 0) {
            return handle_ident(as,args,line);
        }
        if (strcmp(op, ".end") == 0) {
            /* GAS ignores a trailing .end operand but stops the stream. */
            as->source->source_ended=true;
            return true;
        }
        if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 ||
            strcmp(op, ".p2align") == 0) {
            return handle_align(as, op, args, line);
        }
        if (directive_kind == MINIAS_COMMON_DIRECTIVE_ORG) {
            char *rewritten_args = NULL;
            bool ok;

            if (!minias_numeric_refs_rewrite(as,
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

            if (!minias_numeric_refs_rewrite(as,
                                            args,
                                            &rewritten_args,
                                            line)) {
                return false;
            }
            if(strcmp(op,".inst")==0 &&
               !minias_maybe_align_instruction(as,line)) {
                free(rewritten_args);
                return false;
            }
            ok = add_data_stmt(as, op, rewritten_args, line);
            if(ok && strcmp(op,".inst")==0 &&
               as->target->implicit_instruction_alignment!=0U) {
                MiniAsSubsection *sub=minias_ensure_subsection(
                    as,as->current_section,as->current_subsection);
                if(sub==NULL)ok=false;
                else sub->pending_instruction_alignment=false;
            }
            free(rewritten_args);
            return ok;
        }
        minias_set_error(as, "unsupported-directive:%s:line=%zu", op, line);
        return false;
    }

    {
        char *rewritten_args = NULL;
        size_t statement_id;
        bool ok;
        if (!minias_numeric_refs_rewrite(as, args, &rewritten_args, line))
            return false;
        if(!minias_maybe_align_instruction(as,line)) {
            free(rewritten_args);
            return false;
        }
        statement_id=as->stmt_count;
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
        if(!minias_source_graph_lookup_forced_branch(as,
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

static bool parse_loaded_source_graph(MiniAs *as,
                                       MiniAsSourceGraph *graph) {
    MiniAsSourceLine eof={0};
    bool parsed;
    size_t i;
    if(as->source->graph!=NULL || graph==NULL ||
       graph->physical.kind!=MINIAS_SOURCE_FRAME_FILE) {
        minias_set_error(as,"invalid-source-graph-state");
        return false;
    }
    as->source->graph=graph;
    ++graph->parse_passes;
    parsed=minias_source_frame_process(as,&graph->physical);
    if(!parsed)return false;
    if(as->source->conditional_count!=0U) {
        minias_set_error(as,"unterminated-directive:.if:depth=%zu",
                         as->source->conditional_count);
        return false;
    }
    if(as->cfi_active) {
        minias_set_error(as,"unterminated-directive:.cfi_startproc");
        return false;
    }
    /* EOF-scheduled literal pools are generated by a real synthetic source
     * site, just like explicit .ltorg children are owned by their opener. */
    eof.line=graph->physical.count+1U;
    if(!minias_source_graph_line_site(as,&eof,0U,&as->source->current_site))
        return false;
    if(as->target->supports_literal_pools &&
       !flush_literal_pools(as,graph->physical.count,true))return false;
    if(!flush_pending_local_commons(as))return false;
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
            bool code_nop_align=minias_alignment_emits_target_nops(
                as,stmt,section);
            if (!(code_nop_align
                      ? as->target->append_code_alignment(as,stmt,stmt->size)
                      : stmt->align_has_fill
                      ? minias_section_append_fill(as,stmt->section,stmt->size,
                                                   stmt->align_fill,true)
                      : minias_section_append_zero(as,stmt->section,stmt->size))) {
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
    return minias_emit_deferred_leb_relocations(as) &&
           minias_cfi_emit(as) && minias_debug_emit(as);
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
            parsed=minias_source_graph_load_physical(&as,input_path,&graph);
            if(parsed)source_loaded=true;
        }
        if(parsed)parsed=parse_loaded_source_graph(&as,&graph);
        if(parsed)parsed=minias_source_graph_rebind_forced_branches(
                &as,&long_branch_ids,&long_branch_count,
                &long_branch_capacity);
        if(parsed && !minias_solve_fragment_layout(
              &as,&long_branch_ids,&long_branch_count,
              &long_branch_capacity,&added_long_branches,diagnostic))
            parsed=false;
        if(parsed && added_long_branches) {
            parsed=minias_source_graph_capture_forced_branches(&as,long_branch_ids,
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
    minias_source_graph_destroy(&graph);
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
