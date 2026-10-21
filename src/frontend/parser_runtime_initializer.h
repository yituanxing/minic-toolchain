#ifndef MINIC_FRONTEND_PARSER_RUNTIME_INITIALIZER_H
#define MINIC_FRONTEND_PARSER_RUNTIME_INITIALIZER_H
#include "frontend/parser_internal.h"
bool minic_parser_statement_local_lvalue(MinicParser *, MinicLocalId, MinicSourceSpan, MinicExpressionId *);
bool minic_parser_statement_modifiable_lvalue(const MinicExpression *);
bool minic_parser_statement_assignment_conversion(MinicParser *, MinicType, MinicExpressionId *);
bool minic_parser_statement_record_copy(MinicParser *, MinicExpressionId, MinicExpressionId, MinicSourceSpan);
bool minic_parser_statement_record_initialize(MinicParser *, MinicExpressionId, MinicExpressionId, MinicSourceSpan);
bool minic_parser_runtime_local_array(MinicParser *, MinicLocalId, bool);
#endif
