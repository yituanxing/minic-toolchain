#ifndef MINIC_FRONTEND_PARSER_STATIC_INITIALIZER_H
#define MINIC_FRONTEND_PARSER_STATIC_INITIALIZER_H
#include "frontend/parser_internal.h"

bool minic_parser_global_apply_metadata(MinicParser *parser, MinicGlobalObjectId object_id, const char *section_name, size_t section_name_length, bool has_section, size_t explicit_alignment);
bool minic_parser_global_begin_definition(MinicParser *parser, MinicType type, MinicSourceSpan name_span, MinicGlobalObjectId *object_id);
bool minic_parser_global_zero_definition(MinicParser *parser, MinicType object_type, MinicSourceSpan name_span);
bool minic_parser_parse_static_integer_object_initializer(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type);
bool minic_parser_static_string_element_compatible(MinicParser *parser, MinicType element_type, MinicTokenKind literal_kind);
bool minic_parser_static_scalar_array_transaction(MinicParser *parser, MinicGlobalObjectId object_id, MinicType element_type, size_t element_count, bool infer_bound);
bool minic_parser_static_record_initializer(MinicParser *parser, MinicType type, MinicSourceSpan name_span, char *section_name, size_t section_capacity, size_t *section_name_length, bool *has_section, size_t *explicit_alignment);

#endif
