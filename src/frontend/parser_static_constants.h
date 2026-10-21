#ifndef MINIC_FRONTEND_PARSER_STATIC_CONSTANTS_H
#define MINIC_FRONTEND_PARSER_STATIC_CONSTANTS_H
#include "frontend/parser_internal.h"

/* Private static constant decoding boundary. */
bool minic_parser_static_symbol_function_relocation(const MinicC0Program *program, MinicExpressionId expression_id, MinicFunctionId *target_function_id);
bool minic_parser_static_symbol_add_pointer_offset(int64_t base, int64_t delta, int64_t *result);
bool minic_parser_static_symbol_object_relocation_path(const MinicParser *parser, MinicExpressionId expression_id, MinicStaticObjectRelocationTarget *target);
bool minic_parser_static_symbol_pointer_initializer(MinicParser *parser, MinicType target_type, MinicStaticPointerInitializer *initializer);
bool minic_parser_static_symbol_floating_initializer_bits(MinicParser *parser, MinicType target_type, uint64_t *bits);

#endif
