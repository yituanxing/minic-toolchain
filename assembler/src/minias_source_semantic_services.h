#ifndef MINIAS_SOURCE_SEMANTIC_SERVICES_H
#define MINIAS_SOURCE_SEMANTIC_SERVICES_H
#include "minias_target_api.h"
#include <stdarg.h>

/* Source owns its pass-local parser and persistent replay identity. Common
 * lends only the semantic operations that depend on its live SymbolIDs,
 * numeric labels or Target lexical policy. The opaque owner is never
 * inspected or retained by a SourceGraph entry or expansion frame. */
typedef struct MiniAsSourceSemanticServices {
    void *owner;
    char comment_prefix;
    bool preserve_macro_parentheses;
    bool (*evaluate_absolute)(void *,const char *,int64_t *);
    bool (*evaluate_target_if)(void *,const char *,bool *);
    bool (*symbol_defined)(void *,const char *);
    bool (*rewrite_numeric_refs)(void *,const char *,char **,size_t);
    void (*error_va)(void *,const char *,va_list);
} MiniAsSourceSemanticServices;

bool minias_grow_array(void **ptr,size_t *capacity,size_t size,size_t need);
char *minias_strdup(const char *text);
char *minias_trim(char *text);
#endif
