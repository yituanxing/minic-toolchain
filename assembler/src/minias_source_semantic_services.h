#ifndef MINIAS_SOURCE_SEMANTIC_SERVICES_H
#define MINIAS_SOURCE_SEMANTIC_SERVICES_H
#include "minias_target_api.h"
/* Opaque Common session; no writable Section/Symbol/Fragment/TargetOps here. */
typedef struct MiniAs MiniAs;
MiniAsSourceContext *minias_source_session(MiniAs *as);
const MiniAsSourceContext *minias_source_const_session(const MiniAs *as);
/* Target-independent source lexer consumes immutable Target lexical policy. */
char minias_source_target_comment_prefix(const MiniAs *as);
bool minias_source_target_preserve_macro_parentheses(const MiniAs *as);
bool minias_source_target_evaluate_if(const MiniAs *as,
                                     const char *expression,bool *result);
/* The single Common MCExpr/MCLayout services. No Source-side expression
 * parser or Target relaxer is introduced by this boundary. */
bool minias_evaluate_absolute_expression(MiniAs *as,const char *text,
                                         int64_t *value);
bool minias_grow_array(void **ptr,size_t *capacity,size_t size,size_t need);
void minias_set_error(MiniAs *as,const char *format,...);
char *minias_strdup(const char *text);
char *minias_trim(char *text);
#endif
