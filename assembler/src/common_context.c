#include "minias_common_context.h"
#include "minias_common_emission.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The identity maps and their vector capacities never escape Context.
 * Section/Symbol entity IDs remain shared because Layout, CFI and ELF need
 * their semantic records; hash slots are not semantic object state. */
struct MiniAsContextIndexState {
    size_t section_capacity;
    MiniAsSection *section_records; /* same allocation as as->sections */
    size_t *section_slots;
    size_t section_slot_capacity;
    size_t symbol_capacity;
    MiniAsSymbol *symbol_records; /* same allocation as as->symbols */
    size_t *symbol_slots;
    size_t symbol_slot_capacity;
    size_t symbol_name_capacity;
    size_t *symbol_name_slots;
    size_t symbol_name_slot_capacity;
};

bool minias_project_symbol(const MiniAsSymbol *symbol,MiniAsSymbolView *out) {
    if(symbol==NULL || out==NULL)return false;
    *out=(MiniAsSymbolView){
        .name=symbol->name,
        .section=symbol->section,
        .subsection=symbol->subsection,
        .value=symbol->value,
        .bind=symbol->bind,
        .type=symbol->type,
        .defined=symbol->defined,
        .alias_pending=symbol->alias_pending,
        .mutable_absolute_set=symbol->mutable_absolute_set,
        .has_alias_dependency=symbol->has_alias_dependency
    };
    return true;
}

bool minias_common_context_init(MiniAs *as) {
    if(as==NULL)return false;
    if(as->context_indexes!=NULL) {
        minias_set_error(as,"context-index-already-initialized");
        return false;
    }
    as->context_indexes=calloc(1U,sizeof(*as->context_indexes));
    if(as->context_indexes==NULL) {
        minias_set_error(as,"out-of-memory:context-index-state");
        return false;
    }
    return true;
}

void minias_common_context_destroy_sections(MiniAs *as) {
    size_t i;
    for (i = 0U; i < as->section_count; ++i) {
        free((void *)as->sections[i].name);
        free((void *)as->sections[i].group_signature);
        free((void *)as->sections[i].link_order_reference);
    }
    if(as->context_indexes!=NULL)free(as->context_indexes->section_records);
    if(as->context_indexes!=NULL)free(as->context_indexes->section_slots);
}

void minias_common_context_destroy_symbols(MiniAs *as) {
    size_t i;
    for (i = 0U; i < as->symbol_count; ++i) {
        free((void *)as->symbols[i].name);
        free((void *)as->symbols[i].alias_section_base);
    }
    if(as->context_indexes!=NULL)free(as->context_indexes->symbol_records);
    if(as->context_indexes!=NULL)free(as->context_indexes->symbol_slots);
    for (i = 0U; i < as->symbol_name_count; ++i)
        free(as->symbol_names[i].name);
    free(as->symbol_names);
    if(as->context_indexes!=NULL) {
        free(as->context_indexes->symbol_name_slots);
        free(as->context_indexes);
        as->context_indexes=NULL;
    }
}

/* SymbolID edges remain retained across every MCFragment relayout. Resolve
 * one edge only after its target has been normalized in the current pass.
 * This has no ISA knowledge: section-relative aliases and section-cancelled
 * absolute aliases share the same dependency graph and overflow contract. */
static bool resolve_symbol_alias_edge(MiniAs *as, size_t alias_id) {
    MiniAsSymbol *alias = &as->context_indexes->symbol_records[alias_id];
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
    /* GNU snapshots an already-defined target's size at the lexical alias
     * declaration. Only an initially undefined forward target follows its
     * eventual size across relayout. Nonzero `.size alias,N` overrides both,
     * while `.size alias,0` clears that explicit override. */
    if(alias->alias_size_tracks_target && !alias->has_explicit_size)
        alias->size=target->size;
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
static bool minias_refresh_symbol_aliases(MiniAs *as) {
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
            as->context_indexes->symbol_records[i].alias_pending = true;
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

bool minias_common_context_commit_local_symbol_anchors(
            MiniAs *as,const MiniAsSymbolLocalAnchorPlan *plan) {
    size_t i;
    if(as==NULL || plan==NULL || plan->symbol_count!=as->symbol_count ||
       (plan->symbol_count!=0U &&
        (plan->start_local==NULL || plan->end_local==NULL ||
         plan->size_candidate==NULL))) {
        if(as!=NULL)minias_set_error(as,"invalid-symbol-local-anchor-plan");
        return false;
    }
    /* The anchor graph has already validated every candidate address and
     * size. Commit ONLY phase-local positions into the original SymbolIDs.
     * The next Context finalizer adds current subsection bases once. */
    for(i=0U;i<plan->symbol_count;++i) {
        MiniAsSymbol *symbol=&as->context_indexes->symbol_records[i];
        if(symbol->has_size_end_anchor)
            symbol->size=plan->size_candidate[i];
        if(!symbol->has_fragment_anchor || !symbol->defined ||
           symbol->section<0)continue;
        symbol->value=plan->start_local[i];
        if(symbol->has_size_end_anchor)
            symbol->size_end_value=plan->end_local[i];
    }
    return true;
}

bool minias_common_context_finalize_symbol_layout(
            MiniAs *as,const MiniAsSubsectionBaseReader *reader,
            const uint64_t *section_sizes,size_t section_count) {
    size_t i;
    if(as==NULL || reader==NULL || reader->owner==NULL ||
       reader->base==NULL || section_count!=as->section_count ||
       (section_count!=0U && section_sizes==NULL)) {
        if(as!=NULL)minias_set_error(as,"invalid-symbol-layout-snapshot");
        return false;
    }
    for(i=0U;i<as->symbol_count;++i) {
        MiniAsSymbol *symbol=&as->context_indexes->symbol_records[i];
        uint64_t base;
        /* Alias values are owned by the dependency resolver below. Their
         * previous Section-relative values must not get another base. */
        if(!symbol->defined || symbol->section<0 ||
           symbol->has_alias_dependency)continue;
        if(!reader->base(reader->owner,symbol->section,
                         symbol->subsection,&base) ||
           symbol->value>UINT64_MAX-base) {
            minias_set_error(as,"internal:symbol-subsection-layout:%s",
                             symbol->name);
            return false;
        }
        symbol->value+=base;
        if(symbol->has_size_end_anchor && !symbol->has_size_start_symbol) {
            if(symbol->size>UINT64_MAX-symbol->value) {
                minias_set_error(as,"fragment-size-end-overflow:%s",
                                 symbol->name);
                return false;
            }
            symbol->size_end_value=symbol->value+symbol->size;
        }
    }
    if(!minias_refresh_symbol_aliases(as))return false;
    for(i=0U;i<as->symbol_count;++i) {
        MiniAsSymbol *symbol=&as->context_indexes->symbol_records[i];
        const MiniAsSymbol *start;
        if(!symbol->has_size_end_anchor ||
           !symbol->has_size_start_symbol)continue;
        if(symbol->size_start_symbol_index>=as->symbol_count)return false;
        start=&as->symbols[symbol->size_start_symbol_index];
        if(!start->defined || start->section!=symbol->section ||
           start->value>UINT64_MAX-symbol->size) {
            minias_set_error(as,"fragment-size-start-mismatch:%s",
                             symbol->name);
            return false;
        }
        symbol->size_end_value=start->value+symbol->size;
    }
    for(i=0U;i<section_count;++i)
        as->context_indexes->section_records[i].layout_size=section_sizes[i];
    return true;
}


/* SectionID is the identity of one output Section throughout parse, layout
 * and ELF serialization. GNU can create several Sections with the same
 * spelling; group signature, LINK_ORDER source anchor and explicit unique ID
 * all distinguish them. The index is immutable across Section array moves.
 * Absence of unique is different from the valid numeric ID zero. */
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
    free(as->context_indexes->section_slots);
    as->context_indexes->section_slots=slots;
    as->context_indexes->section_slot_capacity=capacity;
    return true;
}

static bool minias_reserve_section_index(MiniAs *as) {
    const size_t cap=as->context_indexes->section_slot_capacity;
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
                (as->context_indexes->section_slot_capacity-1U);
    /* At most half of the table is occupied. An empty slot always exists. */
    while(as->context_indexes->section_slots[slot]!=0U) {
        const size_t section_id=as->context_indexes->section_slots[slot]-1U;
        if(section_key_equal(&as->sections[section_id],key))
            break;
        slot=(slot+1U)&(as->context_indexes->section_slot_capacity-1U);
    }
    return slot;
}

int minias_find_section_key(const MiniAs *as,
                                   const MiniAsSectionIdentity *key) {
    size_t slot;
    if(as==NULL || as->context_indexes==NULL || key==NULL)return -1;
    if(as->context_indexes->section_slot_capacity==0U)return -1;
    slot=minias_section_index_slot(as,key);
    return as->context_indexes->section_slots[slot]==0U ? -1 :
           (int)(as->context_indexes->section_slots[slot]-1U);
}

int minias_find_section(const MiniAs *as,const char *name) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_find_section_key(as,&key);
}

int minias_ensure_section_key(MiniAs *as,
                                     const MiniAsSectionIdentity *key,
                                     uint32_t group_flags,
                                     uint32_t type,uint64_t flags,
                                     uint64_t align,uint64_t entry_size) {
    const int found=minias_find_section_key(as,key);
    MiniAsSection *sec;
    size_t slot;
    if(as==NULL || as->context_indexes==NULL || key==NULL) {
        if(as!=NULL)minias_set_error(as,"section-without-context-index");
        return -1;
    }
    if(found>=0) {
        sec=&as->context_indexes->section_records[(size_t)found];
        if(sec->group_flags!=group_flags) {
            minias_set_error(as,"conflicting-section-group-linkage:%s",key->name);
            return -1;
        }
        /* Preserve GAS first declaration, including sh_entsize. Later
         * conflicting metadata cannot rewrite a live Section identity. */
        (void)type;
        (void)flags;
        (void)entry_size;
        if(align>sec->align)sec->align=align;
        return found;
    }
    if(as->section_count>=(size_t)INT_MAX) {
        minias_set_error(as,"section-count-too-large");
        return -1;
    }
    if(!minias_reserve_section_index(as))return -1;
    if(!minias_grow_array((void **)&as->context_indexes->section_records,
                          &as->context_indexes->section_capacity,
                          sizeof(*as->sections),as->section_count+1U)) {
        minias_set_error(as,"out-of-memory:section");
        return -1;
    }
    as->sections=as->context_indexes->section_records;
    sec=&as->context_indexes->section_records[as->section_count];
    memset(sec,0,sizeof(*sec));
    sec->name=minias_strdup(key->name);
    if(sec->name==NULL) {
        minias_set_error(as,"out-of-memory:section-name");
        return -1;
    }
    if(key->group_signature!=NULL) {
        sec->group_signature=minias_strdup(key->group_signature);
        if(sec->group_signature==NULL) {
            free((void *)sec->name);sec->name=NULL;
            minias_set_error(as,"out-of-memory:group-signature");
            return -1;
        }
    }
    if(key->link_order_reference!=NULL) {
        sec->link_order_reference=minias_strdup(key->link_order_reference);
        if(sec->link_order_reference==NULL) {
            free((void *)sec->group_signature);sec->group_signature=NULL;
            free((void *)sec->name);sec->name=NULL;
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
    sec->entry_size=entry_size;
    /* No failure point remains: publish SectionID and hash entry together. */
    slot=minias_section_index_slot(as,key);
    as->context_indexes->section_slots[slot]=as->section_count+1U;
    ++as->section_count;
    return (int)(as->section_count-1U);
}

int minias_ensure_section(MiniAs *as,
                          const char *name,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t align) {
    const MiniAsSectionIdentity key={name,NULL,NULL,false,0U};
    return minias_ensure_section_key(as,&key,0U,type,flags,align,0U);
}

bool minias_common_context_raise_section_alignment(MiniAs *as,int section,
                                                    uint64_t alignment) {
    MiniAsSection *sec;
    if(as==NULL)return false;
    if(section<0 || (size_t)section>=as->section_count ||
       alignment==0U || (alignment&(alignment-1U))!=0U) {
        minias_set_error(as,"invalid-source-section-alignment");
        return false;
    }
    sec=&as->context_indexes->section_records[(size_t)section];
    if(sec->align<alignment)sec->align=alignment;
    return true;
}

bool minias_common_context_apply_ident_comment(MiniAs *as,int section) {
    MiniAsSection *sec;
    if(as==NULL)return false;
    if(section<0 || (size_t)section>=as->section_count ||
       strcmp(as->sections[(size_t)section].name,".comment")!=0) {
        minias_set_error(as,"invalid-ident-comment-section");
        return false;
    }
    sec=&as->context_indexes->section_records[(size_t)section];
    /* GAS upgrades an existing .comment regardless of its prior flags;
     * this is intentionally different from an ordinary .section reopen. */
    sec->flags|=MINIAS_SHF_MERGE|MINIAS_SHF_STRINGS;
    sec->entry_size=1U;
    return true;
}

bool minias_common_context_set_section_alignment(MiniAs *as,int section,
                                                 uint64_t alignment) {
    MiniAsSection *sec;
    if(as==NULL)return false;
    if(section<0 || (size_t)section>=as->section_count ||
       alignment==0U || (alignment&(alignment-1U))!=0U ||
       as->stmt_count!=0U) {
        minias_set_error(as,"invalid-initial-section-alignment");
        return false;
    }
    sec=&as->context_indexes->section_records[(size_t)section];
    if(minias_emission_section_size(as,section)!=0U || sec->layout_size!=0U) {
        minias_set_error(as,"section-already-materialized");
        return false;
    }
    sec->align=alignment;
    return true;
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
        if (as->symbols[i].unindexed_mapping) continue;
        size_t slot = symbol_name_hash(as->symbols[i].name) & (capacity - 1U);
        while (slots[slot] != 0U) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        slots[slot] = i + 1U;
    }
    free(as->context_indexes->symbol_slots);
    as->context_indexes->symbol_slots = slots;
    as->context_indexes->symbol_slot_capacity = capacity;
    return true;
}

static bool find_symbol_slot(const MiniAs *as,
                             const char *name,
                             size_t *slot_out,
                             size_t *index_out) {
    size_t slot;
    size_t scanned;

    if (as->context_indexes->symbol_slot_capacity == 0U) {
        *slot_out = 0U;
        return false;
    }
    slot = symbol_name_hash(name) & (as->context_indexes->symbol_slot_capacity - 1U);
    for (scanned = 0U; scanned < as->context_indexes->symbol_slot_capacity; ++scanned) {
        size_t encoded = as->context_indexes->symbol_slots[slot];
        if (encoded == 0U) {
            *slot_out = slot;
            return false;
        }
        if (strcmp(as->symbols[encoded - 1U].name, name) == 0) {
            *slot_out = slot;
            *index_out = encoded - 1U;
            return true;
        }
        slot = (slot + 1U) & (as->context_indexes->symbol_slot_capacity - 1U);
    }
    *slot_out = 0U;
    return false;
}

static MiniAsSymbol *context_get_symbol_mut(MiniAs *as, const char *name,
                                            bool create) {
    size_t slot = 0U;
    size_t index = 0U;
    MiniAsSymbol *sym;
    if(as==NULL || as->context_indexes==NULL || name==NULL) {
        if(as!=NULL && create)minias_set_error(as,"symbol-without-context-index");
        return NULL;
    }

    if (find_symbol_slot(as, name, &slot, &index)) {
        return &as->context_indexes->symbol_records[index];
    }
    if (!create) {
        return NULL;
    }
    if (as->context_indexes->symbol_slot_capacity == 0U ||
        as->symbol_count + 1U >= as->context_indexes->symbol_slot_capacity / 2U) {
        size_t next_capacity =
            as->context_indexes->symbol_slot_capacity == 0U ? 16U
                                          : as->context_indexes->symbol_slot_capacity * 2U;
        if (next_capacity < as->context_indexes->symbol_slot_capacity ||
            !rebuild_symbol_index(as, next_capacity)) {
            return NULL;
        }
        if (find_symbol_slot(as, name, &slot, &index)) {
            return &as->context_indexes->symbol_records[index];
        }
    }
    if (!minias_grow_array((void **)&as->context_indexes->symbol_records,
                    &as->context_indexes->symbol_capacity,
                    sizeof(*as->symbols),
                    as->symbol_count + 1U)) {
        minias_set_error(as, "out-of-memory:symbol");
        return NULL;
    }
    /* Reallocation can move the sole table; publish its const projection
     * immediately, including the failed-name case below. */
    as->symbols=as->context_indexes->symbol_records;
    sym = &as->context_indexes->symbol_records[as->symbol_count];
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
    as->context_indexes->symbol_slots[slot] = as->symbol_count;
    return sym;
}

const MiniAsSymbol *minias_common_context_find_symbol(const MiniAs *as,
                                                       const char *name) {
    size_t slot=0U,index=0U;
    if(as==NULL || as->context_indexes==NULL || name==NULL ||
       !find_symbol_slot(as,name,&slot,&index))return NULL;
    return &as->symbols[index];
}

const MiniAsSymbol *minias_common_context_intern_symbol(MiniAs *as,
                                                         const char *name) {
    return context_get_symbol_mut(as,name,true);
}

bool minias_common_context_declare_symbol_binding(MiniAs *as,const char *name,
                                                  bool update_binding,uint8_t bind,
                                                  bool update_visibility,
                                                  uint8_t visibility) {
    MiniAsSymbol *symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    if(update_binding && !(bind==MINIAS_STB_LOCAL && symbol->defined &&
                           symbol->section==MINIAS_SECTION_COMMON)) {
        /* A late `.local` does not retag a GNU tentative SHN_COMMON. */
        symbol->bind=bind;
        symbol->explicit_local_binding=bind==MINIAS_STB_LOCAL;
    }
    if(update_visibility)symbol->visibility=visibility;
    return true;
}

bool minias_common_context_declare_symbol_type(MiniAs *as,const char *name,
                                               uint8_t type,bool force_binding,
                                               uint8_t binding) {
    MiniAsSymbol *symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    symbol->type=type;
    if(force_binding)symbol->bind=binding;
    return true;
}

bool minias_common_context_declare_symbol_size(MiniAs *as,const char *name,
                                               uint64_t size) {
    MiniAsSymbol *symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    symbol->size=size;
    symbol->has_explicit_size=size!=0U;
    return true;
}

bool minias_common_context_capture_symbol_size_end(MiniAs *as,
                 size_t symbol_id,size_t start_id,uint32_t end_subsection,
                 size_t end_ordinal,int64_t end_addend,uint64_t local_size,
                 bool deferred) {
    MiniAsSymbol *symbol;
    if(as==NULL || symbol_id>=as->symbol_count ||
       start_id>=as->symbol_count) {
        if(as!=NULL)minias_set_error(as,"invalid-symbol-size-end-id");
        return false;
    }
    symbol=&as->context_indexes->symbol_records[symbol_id];
    if(!symbol->defined || !as->symbols[start_id].defined ||
       symbol->section!=as->symbols[start_id].section) {
        minias_set_error(as,"invalid-symbol-size-end-state");
        return false;
    }
    symbol->has_size_start_symbol=start_id!=symbol_id;
    symbol->size_start_symbol_index=start_id;
    symbol->size=deferred?0U:local_size;
    symbol->has_explicit_size=true;
    symbol->has_size_end_anchor=true;
    symbol->size_end_anchor=end_ordinal;
    symbol->size_end_subsection=end_subsection;
    symbol->size_end_addend=end_addend;
    return true;
}

bool minias_common_context_define_source_fragment_anchor(MiniAs *as,
             const char *name,MiniAsSourceAnchorKind kind,int section,
             uint32_t subsection,uint64_t value,size_t fragment_ordinal,
             int64_t addend,size_t line,bool hidden,size_t *id_out) {
    MiniAsSymbol *symbol;
    if(as==NULL || name==NULL || section<0 ||
       (size_t)section>=as->section_count ||
       (fragment_ordinal==SIZE_MAX && kind!=MINIAS_SOURCE_SET_DOT)) {
        if(as!=NULL)minias_set_error(as,"invalid-source-symbol-anchor");
        return false;
    }
    symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    if(symbol->defined || symbol->alias_pending || symbol->named_dwarf_view) {
        minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
        return false;
    }
    symbol->defined=true;
    symbol->section=section;
    symbol->subsection=subsection;
    symbol->value=value;
    if(fragment_ordinal!=SIZE_MAX) {
        symbol->has_fragment_anchor=true;
        symbol->fragment_anchor=fragment_ordinal;
        symbol->fragment_anchor_addend=addend;
    }
    if(hidden)symbol->visibility=MINIAS_STV_HIDDEN;
    if(id_out!=NULL)*id_out=(size_t)(symbol-as->symbols);
    return true;
}

bool minias_common_context_assign_symbol_absolute(MiniAs *as,const char *name,
             uint64_t bits,MiniAsAbsoluteAssignKind kind,size_t line) {
    MiniAsSymbol *symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    if(symbol->named_dwarf_view)goto duplicate;
    switch(kind) {
    case MINIAS_ABSOLUTE_EQU_IDEMPOTENT:
        if(symbol->defined || symbol->alias_pending) {
            if(symbol->defined && symbol->section==MINIAS_SECTION_ABS &&
               symbol->value==bits)return true;
            goto duplicate;
        }
        break;
    case MINIAS_ABSOLUTE_SET_MUTABLE:
        if(symbol->alias_pending ||
           (symbol->defined && (symbol->section!=MINIAS_SECTION_ABS ||
                                !symbol->mutable_absolute_set)))goto duplicate;
        break;
    case MINIAS_ABSOLUTE_SET_ONCE:
        if(symbol->defined || symbol->alias_pending)goto duplicate;
        break;
    default:
        minias_set_error(as,"invalid-absolute-symbol-assignment");
        return false;
    }
    symbol->defined=true;
    symbol->section=MINIAS_SECTION_ABS;
    symbol->subsection=0U;
    symbol->value=bits;
    if(kind==MINIAS_ABSOLUTE_SET_MUTABLE)symbol->mutable_absolute_set=true;
    return true;
duplicate:
    minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
    return false;
}

bool minias_common_context_bind_symbol_alias(MiniAs *as,const char *name,
             size_t target_id,int64_t addend,const char *section_base,
             size_t line) {
    MiniAsSymbol *symbol;
    char *base_copy=NULL;
    bool target_size_frozen;
    uint64_t target_size;
    if(as==NULL || target_id>=as->symbol_count || name==NULL) {
        if(as!=NULL)minias_set_error(as,"invalid-symbol-alias-target");
        return false;
    }
    /* An anchor-derived target size is still moving even if its label and
     * `.size` statement already exist. A defined label with no size plan
     * instead provides a lexical zero snapshot (GNU GAS). Forward aliases
     * and inherited alias chains track the current target generation. */
    target_size_frozen=as->symbols[target_id].defined &&
        !as->symbols[target_id].has_size_end_anchor &&
        !(as->symbols[target_id].alias_size_tracks_target &&
          !as->symbols[target_id].has_explicit_size);
    target_size=as->symbols[target_id].size;
    if(section_base!=NULL) {
        base_copy=minias_strdup(section_base);
        if(base_copy==NULL) {
            minias_set_error(as,"out-of-memory:section-set");
            return false;
        }
    }
    symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL) {
        free(base_copy);
        return false;
    }
    if(symbol->defined || symbol->alias_pending ||
       symbol->has_alias_dependency || symbol->named_dwarf_view) {
        minias_set_error(as,"duplicate-symbol:%s:line=%zu",name,line);
        free(base_copy);
        return false;
    }
    symbol->has_alias_dependency=true;
    symbol->alias_pending=true;
    symbol->alias_target_index=target_id;
    symbol->alias_addend=addend;
    symbol->alias_line=line;
    symbol->alias_section_base=base_copy;
    symbol->alias_size_tracks_target=!target_size_frozen;
    if(target_size_frozen && !symbol->has_explicit_size)symbol->size=target_size;
    return true;
}

bool minias_common_context_declare_common(MiniAs *as,const char *name,
               uint64_t size,uint64_t alignment,size_t line,
               MiniAsCommonDeclaration *out) {
    MiniAsSymbol *symbol;
    int bss;
    if(as==NULL || out==NULL || alignment==0U ||
       (alignment&(alignment-1U))!=0U || name==NULL)return false;
    *out=(MiniAsCommonDeclaration){0};
    symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    if(symbol->named_dwarf_view || symbol->alias_pending ||
       symbol->type==MINIAS_STT_FUNC ||
       symbol->bind==MINIAS_STB_WEAK)goto invalid;
    out->symbol_id=(size_t)(symbol-as->symbols);
    if(symbol->defined) {
        if(symbol->section!=MINIAS_SECTION_COMMON)goto invalid;
        if(alignment>symbol->value)symbol->value=alignment;
        return true;
    }
    if(symbol->explicit_local_binding) {
        if(size>UINT32_MAX || alignment>UINT32_MAX)goto invalid;
        bss=minias_ensure_section(as,".bss",MINIAS_SHT_NOBITS,
                                  MINIAS_SHF_ALLOC|MINIAS_SHF_WRITE,1U);
        if(bss<0)return false;
        symbol->defined=true;
        symbol->section=bss;
        symbol->subsection=0U;
        symbol->value=0U;
        symbol->type=MINIAS_STT_OBJECT;
        symbol->size=size;
        out->defer_local_bss=true;
        out->bss_section=bss;
        return true;
    }
    symbol->defined=true;
    symbol->bind=MINIAS_STB_GLOBAL;
    symbol->type=MINIAS_STT_OBJECT;
    symbol->section=MINIAS_SECTION_COMMON;
    symbol->subsection=0U;
    symbol->size=size;
    symbol->value=alignment;
    return true;
invalid:
    minias_set_error(as,"unsupported-common-symbol:%s:line=%zu",name,line);
    return false;
}

bool minias_common_context_finish_local_common(MiniAs *as,size_t symbol_id,
                              int bss_section,uint64_t local_value,
                              size_t fragment_ordinal) {
    MiniAsSymbol *symbol;
    if(as==NULL || symbol_id>=as->symbol_count || bss_section<0 ||
       (size_t)bss_section>=as->section_count) {
        if(as!=NULL)minias_set_error(as,"pending-local-common-invalid-symbol");
        return false;
    }
    symbol=&as->context_indexes->symbol_records[symbol_id];
    if(!symbol->defined || symbol->section!=bss_section ||
       symbol->has_fragment_anchor) {
        minias_set_error(as,"pending-local-common-mutated:%s",symbol->name);
        return false;
    }
    symbol->value=local_value;
    symbol->subsection=0U;
    symbol->has_fragment_anchor=true;
    symbol->fragment_anchor=fragment_ordinal;
    symbol->fragment_anchor_addend=0;
    return true;
}

bool minias_common_context_define_output_anchor(MiniAs *as,const char *name,
                           int section,uint32_t subsection,uint64_t offset,
                           bool hidden,size_t *id_out) {
    MiniAsSymbol *symbol;
    if(as==NULL || section<0 || (size_t)section>=as->section_count ||
       name==NULL) {
        if(as!=NULL)minias_set_error(as,"invalid-final-symbol-anchor");
        return false;
    }
    symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL)return false;
    if(symbol->defined || symbol->alias_pending || symbol->named_dwarf_view) {
        minias_set_error(as,"duplicate-final-symbol-anchor:%s",name);
        return false;
    }
    symbol->defined=true;
    symbol->section=section;
    symbol->subsection=subsection;
    symbol->value=offset;
    symbol->bind=MINIAS_STB_LOCAL;
    if(hidden)symbol->visibility=MINIAS_STV_HIDDEN;
    if(id_out!=NULL)*id_out=(size_t)(symbol-as->symbols);
    return true;
}

bool minias_common_context_reserve_named_dwarf_view(MiniAs *as,
                       const char *name,size_t *id_out) {
    MiniAsSymbol *symbol;
    if(as==NULL || name==NULL || id_out==NULL)return false;
    symbol=context_get_symbol_mut(as,name,true);
    if(symbol==NULL || symbol->defined || symbol->alias_pending ||
       symbol->named_dwarf_view)return false;
    symbol->named_dwarf_view=true;
    *id_out=(size_t)(symbol-as->symbols);
    return true;
}

bool minias_common_context_publish_dwarf_view(MiniAs *as,size_t id,
                                               uint64_t ordinal) {
    MiniAsSymbol *symbol;
    if(as==NULL || id>=as->symbol_count ||
       !as->symbols[id].named_dwarf_view) {
        if(as!=NULL)minias_set_error(as,"dwarf-view-invalid-symbol");
        return false;
    }
    symbol=&as->context_indexes->symbol_records[id];
    symbol->defined=true;
    symbol->section=MINIAS_SECTION_ABS;
    symbol->subsection=0U;
    symbol->value=ordinal;
    return true;
}

bool minias_common_context_bind_fixup_reference(MiniAs *as,size_t id) {
    MiniAsSymbol *symbol;
    if(as==NULL || id>=as->symbol_count) {
        if(as!=NULL)minias_set_error(as,"invalid-fixup-symbol-id:%zu",id);
        return false;
    }
    symbol=&as->context_indexes->symbol_records[id];
    if(!symbol->defined && symbol->bind==MINIAS_STB_LOCAL &&
       !symbol->explicit_local_binding &&
       strncmp(symbol->name,".L",2U)!=0)
        symbol->bind=MINIAS_STB_GLOBAL;
    return true;
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
        if (minias_common_context_find_symbol(as,name) == NULL) break;
        if (ordinal == SIZE_MAX) {
            minias_set_error(as, "target-mapping-symbol-ordinal-overflow");
            return false;
        }
        ++ordinal;
    }
    symbol = context_get_symbol_mut(as,name,true);
    if (symbol == NULL) return false;
    symbol->defined = true;
    symbol->section = section;
    symbol->value = offset;
    symbol->bind = MINIAS_STB_LOCAL;
    symbol->type = MINIAS_STT_NOTYPE;
    return true;
}

/* A Target may request this ABI alignment property during object
 * finalization, but Section storage and mutation remain Common-owned. */
static bool minias_require_target_section_alignment(MiniAs *as, int section,
                                                     uint64_t alignment) {
    if(!as->finalizing_target_output || section<0 ||
       (size_t)section>=as->section_count || alignment==0U ||
       (alignment&(alignment-1U))!=0U) {
        minias_set_error(as,"invalid-target-finalization-alignment");
        return false;
    }
    if(as->sections[(size_t)section].align<alignment)
        as->context_indexes->section_records[(size_t)section].align=alignment;
    return true;
}

size_t minias_post_emit_symbol_count(const MiniAsPostEmitObjectSink *sink) {
    return sink->as->symbol_count;
}

bool minias_post_emit_define_mapping_symbol(MiniAsPostEmitObjectSink *sink,
                                            const char *stem,
                                            size_t first_ordinal,
                                            bool always_suffix,
                                            int section, uint64_t offset) {
    if (!sink->as->finalizing_target_output) {
        minias_set_error(sink->as, "invalid-target-mapping-phase");
        return false;
    }
    return minias_define_target_mapping_symbol(sink->as, stem, first_ordinal,
                                               always_suffix, section, offset);
}

bool minias_post_emit_append_exact_mapping_symbol(
    MiniAsPostEmitObjectSink *sink,const char *name,int section,uint64_t offset) {
    MiniAs *as;
    MiniAsSymbol *symbol;
    if(sink==NULL || sink->as==NULL)return false;
    as=sink->as;
    if(!as->finalizing_target_output || name==NULL || *name=='\0' ||
       section<0 || (size_t)section>=as->section_count) {
        minias_set_error(as,"invalid-exact-target-mapping-symbol");
        return false;
    }
    /* Keep these ELF-only symbols out of the source-name hash: a repeated
     * $x<isa> is a distinct source transition, not a redefinition. */
    if(!minias_grow_array((void **)&as->context_indexes->symbol_records,
                          &as->context_indexes->symbol_capacity,
                          sizeof(*as->symbols),as->symbol_count+1U)) {
        minias_set_error(as,"out-of-memory:exact-target-mapping");
        return false;
    }
    /* The Context owns the only mutable table. Publish its const view
     * after growth, exactly as ordinary Context interning does. */
    as->symbols=as->context_indexes->symbol_records;
    symbol=&as->context_indexes->symbol_records[as->symbol_count];
    memset(symbol,0,sizeof(*symbol));
    symbol->name=minias_strdup(name);
    if(symbol->name==NULL) {
        minias_set_error(as,"out-of-memory:exact-target-mapping-name");
        return false;
    }
    symbol->unindexed_mapping=true;
    symbol->defined=true;
    symbol->section=section;
    symbol->value=offset;
    symbol->bind=MINIAS_STB_LOCAL;
    symbol->type=MINIAS_STT_NOTYPE;
    ++as->symbol_count;
    return true;
}

bool minias_post_emit_raise_section_alignment(MiniAsPostEmitObjectSink *sink,
                                              int section, uint64_t align) {
    /* Reuse the guarded Common alignment policy introduced in the
     * preceding Fixup/finalization batch; do not make a second owner. */
    return minias_require_target_section_alignment(sink->as, section, align);
}

/* Stable parse-time NameIds are intentionally NOT Object SymbolIDs.  A
 * machine operand may name a forward or external symbol during parsing,
 * while the historical ELF symbol order is determined later by definitions
 * and committed fixups.  Interning here may allocate only this name pool;
 * it must never intern a new SymbolID. */
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
    free(as->context_indexes->symbol_name_slots);
    as->context_indexes->symbol_name_slots=slots;
    as->context_indexes->symbol_name_slot_capacity=capacity;
    return true;
}

bool minias_symbol_name_intern(MiniAs *as,const char *name,
                               MiniAsSymbolNameId *name_id) {
    size_t slot,scanned;
    if(as==NULL || name==NULL || *name=='\0' || name_id==NULL)return false;
    if(as->context_indexes==NULL) {
        minias_set_error(as,"symbol-name-without-context-index");
        return false;
    }
    /* A repeated operand must not trigger a table expansion merely because
     * the pool is near its load threshold.  Name lookup has no side effect. */
    if(as->context_indexes->symbol_name_slot_capacity!=0U) {
        slot=symbol_name_hash(name)&(as->context_indexes->symbol_name_slot_capacity-1U);
        for(scanned=0U;scanned<as->context_indexes->symbol_name_slot_capacity;++scanned) {
            size_t encoded=as->context_indexes->symbol_name_slots[slot];
            if(encoded==0U)break;
            if(strcmp(as->symbol_names[encoded-1U].name,name)==0) {
                *name_id=encoded-1U;
                return true;
            }
            slot=(slot+1U)&(as->context_indexes->symbol_name_slot_capacity-1U);
        }
    }
    if(as->symbol_name_count==SIZE_MAX) {
        minias_set_error(as,"symbol-name-count-overflow");
        return false;
    }
    if(as->context_indexes->symbol_name_slot_capacity==0U ||
       as->symbol_name_count+1U>=as->context_indexes->symbol_name_slot_capacity/2U) {
        size_t next=as->context_indexes->symbol_name_slot_capacity==0U?16U:
                    as->context_indexes->symbol_name_slot_capacity*2U;
        if(next<as->context_indexes->symbol_name_slot_capacity ||
           !rebuild_symbol_name_index(as,next))return false;
    }
    slot=symbol_name_hash(name)&(as->context_indexes->symbol_name_slot_capacity-1U);
    for(scanned=0U;scanned<as->context_indexes->symbol_name_slot_capacity;++scanned) {
        size_t encoded=as->context_indexes->symbol_name_slots[slot];
        if(encoded==0U)break;
        slot=(slot+1U)&(as->context_indexes->symbol_name_slot_capacity-1U);
    }
    if(scanned==as->context_indexes->symbol_name_slot_capacity ||
       as->symbol_name_count==SIZE_MAX ||
       !minias_grow_array((void **)&as->symbol_names,
                          &as->context_indexes->symbol_name_capacity,
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
    as->context_indexes->symbol_name_slots[slot]=++as->symbol_name_count;
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
