#ifndef MINIC_FRONTEND_PARSER_INTERNAL_H
#define MINIC_FRONTEND_PARSER_INTERNAL_H

#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/token.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

#define MINIC_PARSER_MAX_SWITCH_DEPTH 16U
#define MINIC_PARSER_MAX_SWITCH_CASES 128U

typedef struct MinicParserLocalBinding {
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicGlobalObjectId global_object_id;
} MinicParserLocalBinding;

typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    int value;
} MinicParserEnumConstant;

typedef struct MinicParserSwitchContext {
    int case_values[MINIC_PARSER_MAX_SWITCH_CASES];
    size_t case_count;
    bool has_default;
} MinicParserSwitchContext;

typedef struct MinicParser {
    const char *path;
    const char *source;
    MinicLexer lexer;
    MinicToken current;
    MinicDiagnostic *diagnostic;
    MinicC0Program *program;
    MinicBlockId current_block;
    MinicFunctionId current_function;
    size_t local_begin;
    size_t loop_depth;
    size_t switch_depth;
    MinicParserSwitchContext switch_contexts[MINIC_PARSER_MAX_SWITCH_DEPTH];

    bool label_context_initialized;
    MinicFunctionId label_context_function;
    size_t function_statement_begin;

    MinicParserLocalBinding *local_bindings;
    size_t local_binding_count;
    size_t local_binding_capacity;

    size_t *scope_binding_begins;
    size_t scope_count;
    size_t scope_capacity;

    MinicParserEnumConstant *enum_constants;
    size_t enum_constant_count;
    size_t enum_constant_capacity;
} MinicParser;

void minic_parser_error(MinicParser *parser, const char *format, ...);
bool minic_parser_advance(MinicParser *parser);
bool minic_parser_expect(MinicParser *parser, MinicTokenKind kind, const char *message);
bool minic_parser_parse_integer_value(MinicParser *parser, int *value);
bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type);
bool minic_parser_parse_pointer_declarator(MinicParser *parser,
                                           MinicType base_type,
                                           MinicType *type);
bool minic_parser_parse_type_name(MinicParser *parser, MinicType *type);
bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message);
bool minic_parser_parse_parameter_list(MinicParser *parser,
                                       MinicSourceSpan *parameter_name_spans,
                                       MinicType *parameter_types,
                                       size_t *parameter_count,
                                       bool require_names,
                                       bool *is_variadic);
bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
size_t minic_parser_span_length(MinicSourceSpan span);
bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right);
bool minic_parser_add_expression(MinicParser *parser,
                                 const MinicExpression *expression,
                                 MinicExpressionId *expression_id);
bool minic_parser_add_statement(MinicParser *parser, const MinicStatement *statement);

bool minic_parser_begin_scope(MinicParser *parser);
void minic_parser_end_scope(MinicParser *parser);
bool minic_parser_bind_local(MinicParser *parser, MinicSourceSpan name_span, MinicLocalId local_id);
bool minic_parser_bind_static_local(MinicParser *parser,
                                    MinicSourceSpan name_span,
                                    MinicGlobalObjectId global_object_id);
bool minic_parser_name_bound_in_current_scope(const MinicParser *parser, MinicSourceSpan name_span);
MinicLocalId minic_parser_find_local_in_current_scope(const MinicParser *parser,
                                                      MinicSourceSpan name_span);
void minic_parser_destroy_scopes(MinicParser *parser);
bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value);
bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value);
void minic_parser_destroy_enum_constants(MinicParser *parser);

MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span);
MinicGlobalObjectId minic_parser_find_static_local(const MinicParser *parser,
                                                   MinicSourceSpan name_span);
bool minic_parser_name_bound(const MinicParser *parser, MinicSourceSpan name_span);
MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span);
MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span);
MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span);
MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser, MinicSourceSpan name_span);

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type);
bool minic_parser_parse_record_definition(MinicParser *parser);
bool minic_parser_parse_enum_definition(MinicParser *parser);
bool minic_parser_parse_typedef(MinicParser *parser);
bool minic_parser_parse_static_global(MinicParser *parser);
bool minic_parser_parse_pointer_member(MinicParser *parser,
                                       MinicExpressionId base_id,
                                       MinicExpressionId *expression_id);
bool minic_parser_parse_direct_member(MinicParser *parser,
                                      MinicExpressionId base_id,
                                      MinicExpressionId *expression_id);
bool minic_parser_apply_array_decay(MinicParser *parser,
                                    MinicExpressionId input_id,
                                    MinicExpressionId *expression_id);
bool minic_parser_parse_postfix(MinicParser *parser,
                                MinicExpressionId base_id,
                                MinicExpressionId *expression_id);
bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
bool minic_parser_add_default_return(MinicParser *parser);
bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);

#endif