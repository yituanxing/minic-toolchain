#ifndef MINIC_FRONTEND_PARSER_INTERNAL_H
#define MINIC_FRONTEND_PARSER_INTERNAL_H

#include "frontend/ast.h"
#include "frontend/attribute.h"
#include "frontend/const_eval.h"
#include "frontend/lexer.h"
#include "frontend/token.h"
#include "minic/compiler.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MINIC_PARSER_MAX_SWITCH_DEPTH 16U
#define MINIC_PARSER_MAX_SWITCH_CASES 128U
#define MINIC_MAX_PARSED_ATTRIBUTES 32U
#define MINIC_RECORD_MEMBER_MAX_DEPTH 8U

typedef struct MinicRecordFieldPath {
    MinicRecordId record_ids[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t field_indices[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t depth;
    bool found;
    bool ambiguous;
} MinicRecordFieldPath;

typedef struct MinicParserLocalBinding {
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicGlobalObjectId global_object_id;
} MinicParserLocalBinding;

typedef struct MinicParserRecordTag {
    MinicSourceSpan name_span;
    MinicRecordId record_id;
} MinicParserRecordTag;

typedef struct MinicParserScopeFrame {
    size_t binding_begin;
    size_t record_tag_begin;
    MinicCleanupContextId cleanup_context;
} MinicParserScopeFrame;

typedef struct MinicParserLocalLabel {
    MinicSourceSpan name_span;
    MinicStatementId statement_id;
    size_t scope_depth;
    bool is_active;
    bool is_defined;
} MinicParserLocalLabel;

typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    MinicEnumeratorId enumerator_id;
} MinicParserEnumConstant;

typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
    MinicEnumId enum_id;
} MinicParserEnumTag;

typedef struct MinicParserSwitchContext {
    int64_t case_values[MINIC_PARSER_MAX_SWITCH_CASES];
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
    const MinicTargetInfo *target_info;
    MinicBlockId current_block;
    MinicFunctionId current_function;
    MinicGlobalObjectId current_function_name_object;
    size_t local_begin;
    size_t loop_depth;
    MinicStatementId continue_target_statement;
    MinicCleanupContextId cleanup_context;
    MinicCleanupContextId break_cleanup_context;
    MinicCleanupContextId continue_cleanup_context;
    size_t statement_expression_depth;
    size_t switch_depth;
    size_t record_pack_alignment;
    MinicParserSwitchContext switch_contexts[MINIC_PARSER_MAX_SWITCH_DEPTH];

    bool label_context_initialized;
    MinicFunctionId label_context_function;
    size_t function_statement_begin;

    MinicParserLocalLabel *local_labels;
    size_t local_label_count;
    size_t local_label_capacity;

    MinicParserLocalBinding *local_bindings;
    size_t local_binding_count;
    size_t local_binding_capacity;

    MinicParserRecordTag *record_tags;
    size_t record_tag_count;
    size_t record_tag_capacity;

    MinicParserScopeFrame *scopes;
    size_t scope_count;
    size_t scope_capacity;

    MinicParserEnumConstant *enum_constants;
    size_t enum_constant_count;
    size_t enum_constant_capacity;

    MinicParserEnumTag *enum_tags;
    size_t enum_tag_count;
    size_t enum_tag_capacity;
} MinicParser;

typedef struct MinicParsedAttribute {
    const MinicAttributeDescriptor *descriptor;
    MinicSourceSpan name_span;
    MinicSourceSpan arguments_span;
    bool has_arguments;
} MinicParsedAttribute;

typedef struct MinicParsedAttributeList {
    MinicParsedAttribute values[MINIC_MAX_PARSED_ATTRIBUTES];
    size_t count;
} MinicParsedAttributeList;

typedef bool (*MinicParsedAttributeConsumer)(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             void *context);

typedef struct MinicParsedFunctionDeclarator {
    MinicSourceSpan name_span;
    MinicParsedAttributeList attributes;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_count;
    size_t pointer_depth;
    unsigned int pointer_const_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    bool has_name;
    bool is_variadic;
} MinicParsedFunctionDeclarator;

void minic_parser_error(MinicParser *parser, const char *format, ...);
bool minic_parser_advance(MinicParser *parser);
bool minic_parser_expect(MinicParser *parser, MinicTokenKind kind, const char *message);
bool minic_parser_parse_integer_value(MinicParser *parser, int *value);
bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);
bool minic_parser_parse_zero_pointer_constant(MinicParser *parser);
bool minic_parser_parse_null_pointer_constant_expression(MinicParser *parser,
                                                         MinicType target_type);
bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value);
bool minic_parser_current_integer_literal_syntax(const MinicParser *parser,
                                                 MinicIntegerLiteralBase *base,
                                                 bool *has_unsigned_suffix,
                                                 unsigned int *long_count);
bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);
bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,
                                                 MinicType target_type,
                                                 uint64_t *bits);
bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);
bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value);
bool minic_parser_parse_alignof_type_value(MinicParser *parser,
                                           int64_t *value,
                                           MinicSourceSpan *span);
bool minic_parser_token_starts_type_name(const MinicParser *parser, MinicToken token);
bool minic_parser_token_starts_declaration_specifiers(const MinicParser *parser, MinicToken token);
bool minic_parser_parse_local_storage_class(MinicParser *parser, bool *is_register_storage);
bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type);
bool minic_parser_parse_pointer_qualifier_sequence(MinicParser *parser,
                                                   size_t pointer_depth,
                                                   unsigned int *const_qualifiers,
                                                   unsigned int *volatile_qualifiers);
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
bool minic_parser_function_signature_matches(const MinicFunction *function,
                                             MinicType return_type,
                                             const MinicType *parameter_types,
                                             size_t parameter_count,
                                             bool is_variadic);
bool minic_parser_parse_gnu_function_attributes(MinicParser *parser);
bool minic_parser_parse_gnu_section_attribute(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_section);
bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                       bool is_internal,
                                                       bool is_inline);
bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value);
bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
bool minic_parser_parse_record_array_bound(MinicParser *parser,
                                           size_t *element_count,
                                           bool *is_zero_length);
size_t minic_parser_span_length(MinicSourceSpan span);
bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right);
const MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser);
bool minic_parser_current_attribute_is(const MinicParser *parser,
                                       MinicAttributeKind kind,
                                       MinicAttributeTarget target);
bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,
                                            MinicParsedAttributeConsumer consumer,
                                            void *context);
bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,
                                              MinicParsedAttributeList *attributes);
bool minic_parser_apply_section_attribute(MinicParser *parser,
                                          const MinicParsedAttribute *attribute,
                                          char *buffer,
                                          size_t capacity,
                                          size_t *length,
                                          bool *has_section);
bool minic_parser_apply_object_attribute_list(MinicParser *parser,
                                              const MinicParsedAttributeList *attributes,
                                              char *section_name,
                                              size_t section_capacity,
                                              size_t *section_name_length,
                                              bool *has_section,
                                              size_t *explicit_alignment);
bool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,
                                                   char *section_name,
                                                   size_t section_capacity,
                                                   size_t *section_name_length,
                                                   bool *has_section,
                                                   size_t *explicit_alignment);
bool minic_parser_apply_alignment_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            const char *subject,
                                            size_t *explicit_alignment);
bool minic_parser_parse_direct_declarator_name(MinicParser *parser, MinicSourceSpan *name_span);
bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,
                                                  MinicParsedFunctionDeclarator *declarator);
bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator);
bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array);
bool minic_parser_build_function_declarator_type(MinicParser *parser,
                                                 MinicType return_type,
                                                 const MinicParsedFunctionDeclarator *declarator,
                                                 MinicType *declarator_type);
bool minic_parser_add_expression(MinicParser *parser,
                                 const MinicExpression *expression,
                                 MinicExpressionId *expression_id);
bool minic_parser_add_statement(MinicParser *parser, const MinicStatement *statement);
bool minic_parser_materialize_cleanup_contexts(MinicParser *parser,
                                               MinicCleanupContextId stop_context);

bool minic_parser_begin_scope(MinicParser *parser);
void minic_parser_end_scope(MinicParser *parser);
bool minic_parser_declare_local_label(MinicParser *parser,
                                      MinicSourceSpan name_span,
                                      MinicStatementId statement_id);
MinicStatementId minic_parser_find_local_label(const MinicParser *parser,
                                               MinicSourceSpan name_span);
bool minic_parser_define_local_label(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicStatementId *statement_id);
bool minic_parser_statement_is_local_label(const MinicParser *parser,
                                           MinicStatementId statement_id);
MinicStatementId minic_parser_find_label_statement(MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_bind_local(MinicParser *parser, MinicSourceSpan name_span, MinicLocalId local_id);
bool minic_parser_bind_scoped_global_object(MinicParser *parser,
                                            MinicSourceSpan name_span,
                                            MinicGlobalObjectId global_object_id);
bool minic_parser_name_bound_in_current_scope(const MinicParser *parser, MinicSourceSpan name_span);
MinicLocalId minic_parser_find_local_in_current_scope(const MinicParser *parser,
                                                      MinicSourceSpan name_span);
void minic_parser_destroy_scopes(MinicParser *parser);
bool minic_parser_bind_enum_constant(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicEnumeratorId enumerator_id);
MinicEnumeratorId minic_parser_find_enum_constant(const MinicParser *parser,
                                                  MinicSourceSpan name_span);
MinicEnumId minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type);
bool minic_parser_find_record_field_path(const MinicParser *parser,
                                         const MinicRecord *record,
                                         MinicSourceSpan name_span,
                                         MinicRecordFieldPath *result);
void minic_parser_destroy_enum_constants(MinicParser *parser);

MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span);
MinicGlobalObjectId minic_parser_find_scoped_global_object(const MinicParser *parser,
                                                           MinicSourceSpan name_span);
MinicGlobalObjectId
minic_parser_find_scoped_global_object_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span);
bool minic_parser_name_bound(const MinicParser *parser, MinicSourceSpan name_span);
MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span);
MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span);
MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,
                                                           MinicSourceSpan name_span);
bool minic_parser_declare_block_scope_extern_object(MinicParser *parser,
                                                    MinicSourceSpan name_span,
                                                    MinicType object_type,
                                                    MinicGlobalObjectId *object_id);
MinicFixedRegisterBindingId minic_parser_find_fixed_register_binding(const MinicParser *parser,
                                                                     MinicSourceSpan name_span);
MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span);
MinicRecordId minic_parser_find_record_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span);
bool minic_parser_bind_record_tag(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicRecordId record_id);
MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser, MinicSourceSpan name_span);

bool minic_parser_parse_type_name_preserving_incomplete(MinicParser *parser, MinicType *type);
bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type);
bool minic_parser_parse_record_definition(MinicParser *parser);
bool minic_parser_parse_enum_definition(MinicParser *parser);
bool minic_parser_parse_typedef(MinicParser *parser);
bool minic_parser_parse_static_global(MinicParser *parser);
bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType object_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment);
bool minic_parser_parse_static_zero_declaration_list_after_head(MinicParser *parser,
                                                                MinicType base_type,
                                                                MinicType first_object_type,
                                                                MinicSourceSpan first_name_span,
                                                                const char *shared_section_name,
                                                                size_t shared_section_name_length,
                                                                bool shared_has_section,
                                                                size_t shared_explicit_alignment);
bool minic_parser_parse_extern_global(MinicParser *parser);
bool minic_parser_parse_extern_global_after_head(MinicParser *parser,
                                                 MinicType base_type,
                                                 MinicType first_object_type,
                                                 MinicSourceSpan first_name_span,
                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 size_t explicit_alignment,
                                                 MinicSymbolVisibility visibility,
                                                 bool has_visibility);
bool minic_parser_parse_pointer_member(MinicParser *parser,
                                       MinicExpressionId base_id,
                                       MinicExpressionId *expression_id);
bool minic_parser_parse_direct_member(MinicParser *parser,
                                      MinicExpressionId base_id,
                                      MinicExpressionId *expression_id);
bool minic_parser_apply_fixed_call_argument_conversion(MinicParser *parser,
                                                       MinicType target_type,
                                                       MinicExpressionId *argument_id);
bool minic_parser_apply_array_decay(MinicParser *parser,
                                    MinicExpressionId input_id,
                                    MinicExpressionId *expression_id);
bool minic_parser_materialize_array_object_type(MinicParser *parser,
                                                MinicExpressionId expression_id,
                                                MinicType *array_type);
bool minic_parser_parse_postfix(MinicParser *parser,
                                MinicExpressionId base_id,
                                MinicExpressionId *expression_id);
bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
bool minic_parser_get_predefined_function_name_object(MinicParser *parser,
                                                      MinicGlobalObjectId *object_id);
bool minic_parser_parse_string_literal_size(MinicParser *parser, uint64_t *size);
bool minic_parser_add_string_literal_initializer(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 size_t *element_count);
bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_string_text(MinicParser *parser,
                                    char **text,
                                    size_t *length,
                                    MinicSourceSpan *span);
bool minic_parser_token_starts_expression(MinicTokenKind kind);
bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence);
bool minic_parser_parse_expression_no_decay(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_full_expression_tail(MinicParser *parser,
                                             MinicExpressionId left,
                                             MinicExpressionId *expression_id);
bool minic_parser_parse_full_expression(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_static_assert_declaration(MinicParser *parser);
bool minic_parser_add_default_return(MinicParser *parser);
bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration);
bool minic_parser_parse_runtime_record_initializer(MinicParser *parser,
                                                   MinicExpressionId target_id);
bool minic_parser_parse_statement_expression(MinicParser *parser,
                                             MinicSourcePosition begin,
                                             MinicExpressionId *expression_id);

#endif