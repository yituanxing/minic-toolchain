#include "minias_common_context.h"

/* The consumer keeps a stable ID and a stack value. It cannot retain an
 * entity pointer or inspect Context's alias DAG through this contract. */
bool query_symbol(const MiniAs *as,size_t id,const char *name,
                  MiniAsSymbolRef ref) {
    MiniAsSymbolView value;
    if(!minias_common_context_symbol_view(as,id,&value))return false;
    if(!minias_common_context_find_symbol_view(as,name,&value))return false;
    return minias_common_context_symbol_ref_view(as,ref,&value) &&
           value.name!=NULL && value.defined;
}

bool intern_symbol(MiniAs *as,const char *name,size_t *id) {
    return minias_common_context_intern_symbol_id(as,name,id);
}

#if defined(TRY_ALIAS_EDGE)
size_t alias_edge(MiniAsSymbolView value) {return value.alias_target_index;}
#elif defined(TRY_ALIAS_BASE)
const char *alias_base(MiniAsSymbolView value) {return value.alias_section_base;}
#elif defined(TRY_FRAGMENT_ANCHOR)
size_t anchor(MiniAsSymbolView value) {return value.fragment_anchor;}
#elif defined(TRY_SIZE_PLAN)
size_t size_start(MiniAsSymbolView value) {return value.size_start_symbol_index;}
#elif defined(TRY_NAME_WRITE)
void change_name(MiniAsSymbolView *value) {value->name[0]='x';}
#elif defined(TRY_ENTITY_BORROW)
const MiniAsSymbol *borrow(const MiniAs *as,size_t id) {
    return minias_common_context_symbol_view(as,id,NULL);
}
#endif
