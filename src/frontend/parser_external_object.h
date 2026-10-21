#ifndef MINIC_FRONTEND_PARSER_EXTERNAL_OBJECT_H
#define MINIC_FRONTEND_PARSER_EXTERNAL_OBJECT_H
#include "frontend/parser_internal.h"

/* File-scope objects, tentative definitions, arrays, visibility and weak
 * metadata are handled separately from functions and translation-unit flow. */
bool minic_parser_external_object_metadata(MinicParser *parser, MinicGlobalObjectId object_id, const char *section_name, size_t section_name_length, bool has_section, size_t explicit_alignment, MinicSymbolVisibility visibility, bool has_visibility);
bool minic_parser_external_object_weak(MinicParser *parser, MinicSourceSpan name_span, bool is_weak);
bool minic_parser_external_tentative_object(MinicParser *parser, MinicType object_type, MinicSourceSpan name_span, const char *section_name, size_t section_name_length, bool has_section, size_t explicit_alignment, MinicSymbolVisibility visibility, bool has_visibility);
bool minic_parser_external_tentative_declaration_list_after_head(MinicParser *parser, MinicType base_type, MinicType first_type, MinicSourceSpan first_name, const char *first_section_name, size_t first_section_name_length, bool first_has_section, size_t first_explicit_alignment, MinicSymbolVisibility first_visibility, bool first_has_visibility);
bool minic_parser_external_object_definition(MinicParser *parser, MinicType object_type, MinicSourceSpan name_span, const char *section_name, size_t section_name_length, bool has_section, size_t explicit_alignment, MinicSymbolVisibility visibility, bool has_visibility);
bool minic_parser_external_visible_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span, char *section_name, size_t section_name_capacity, size_t *section_name_length, bool *has_section, size_t *explicit_alignment, MinicSymbolVisibility visibility, bool has_visibility, bool *is_weak);

#endif
