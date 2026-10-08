#!/usr/bin/env python3
"""Audit-only P11: resolve ordinary identifiers by nearest lexical scope.

C ordinary identifier namespace includes locals, typedefs, block-scope extern
and enumerators.  The parser stores enum and local bindings in distinct
arrays; checking each category independently ignores a closer enum binding.
"""
from pathlib import Path

def patch(path, old, new):
    p=Path(path)
    s=p.read_text()
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"ordinary-scope anchor count={n}: {path}: {old[:100]!r}")
    p.write_text(s.replace(old,new,1))

patch("src/frontend/parser_internal.h",
      """typedef struct MinicParserRecordTag {""",
      """/* Nearest ordinary-identifier binding, across locals, typedefs, scoped
 * extern objects, and enum constants.  IDs have independent INVALID values. */
typedef struct MinicParserOrdinaryBinding {
    MinicLocalId local_id;
    MinicGlobalObjectId global_object_id;
    MinicTypeAliasId type_alias_id;
    MinicEnumeratorId enumerator_id;
} MinicParserOrdinaryBinding;

typedef struct MinicParserRecordTag {""")
patch("src/frontend/parser_internal.h",
      "MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span);",
      """bool minic_parser_find_nearest_ordinary_binding(
    const MinicParser *parser, MinicSourceSpan name_span,
    MinicParserOrdinaryBinding *result);
MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span);""")

helper=r'''
bool minic_parser_find_nearest_ordinary_binding(
    const MinicParser *parser, MinicSourceSpan name_span,
    MinicParserOrdinaryBinding *result) {
    size_t depth;
    if (result == NULL) {
        return false;
    }
    result->local_id = MINIC_LOCAL_INVALID;
    result->global_object_id = MINIC_GLOBAL_OBJECT_INVALID;
    result->type_alias_id = MINIC_TYPE_ALIAS_INVALID;
    result->enumerator_id = MINIC_ENUMERATOR_INVALID;
    if (parser == NULL) {
        return false;
    }

    /* Walk scopes innermost first. Within a legal C scope the ordinary
     * namespace has no conflicting declarations across these two arrays. */
    for (depth = parser->scope_count; depth > 0U; --depth) {
        size_t begin_binding = parser->scopes[depth - 1U].binding_begin;
        size_t end_binding = depth == parser->scope_count
            ? parser->local_binding_count : parser->scopes[depth].binding_begin;
        size_t begin_enum = parser->scopes[depth - 1U].enum_constant_begin;
        size_t end_enum = depth == parser->scope_count
            ? parser->enum_constant_count : parser->scopes[depth].enum_constant_begin;
        size_t index;
        for (index = end_binding; index > begin_binding; --index) {
            const MinicParserLocalBinding *binding =
                &parser->local_bindings[index - 1U];
            if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
                result->local_id = binding->local_id;
                result->global_object_id = binding->global_object_id;
                result->type_alias_id = binding->type_alias_id;
                return true;
            }
        }
        for (index = end_enum; index > begin_enum; --index) {
            const MinicParserEnumConstant *enumerator =
                &parser->enum_constants[index - 1U];
            if (minic_parser_span_equals(parser, name_span, enumerator->name_span)) {
                result->enumerator_id = enumerator->enumerator_id;
                return true;
            }
        }
    }
    return false;
}

'''
patch("src/frontend/parser_core.c",
      "MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {",
      helper+"MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {")

patch("src/frontend/parser_type.c",
      """    bool int128_unsigned;
    MinicParser probe;

    switch (token.kind) {""",
      """    bool int128_unsigned;
    MinicParser probe;
    MinicParserOrdinaryBinding nearest_binding;

    switch (token.kind) {""")
patch("src/frontend/parser_type.c",
      """        return minic_parser_find_local(parser, token.span) == MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;""",
      """        if (minic_parser_find_nearest_ordinary_binding(
                parser, token.span, &nearest_binding)) {
            return nearest_binding.type_alias_id != MINIC_TYPE_ALIAS_INVALID;
        }
        return minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;""")
patch("src/frontend/parser_expression.c",
      """    MinicEnumeratorId enumerator_id;

    if (generic_token_text_equals(parser, "__builtin_va_start")) {""",
      """    MinicEnumeratorId enumerator_id;
    MinicParserOrdinaryBinding nearest_binding;
    bool has_nearest_binding;

    if (generic_token_text_equals(parser, "__builtin_va_start")) {""")
patch("src/frontend/parser_expression.c",
      """        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        if (local_id != MINIC_LOCAL_INVALID) {""",
      """        name_span = parser->current.span;
        has_nearest_binding = minic_parser_find_nearest_ordinary_binding(
            parser, name_span, &nearest_binding);
        if (has_nearest_binding &&
            nearest_binding.type_alias_id != MINIC_TYPE_ALIAS_INVALID) {
            minic_parser_error(parser, "typedef name cannot be used as an expression");
            return false;
        }
        local_id = has_nearest_binding
            ? nearest_binding.local_id
            : minic_parser_find_local(parser, name_span);
        if (local_id != MINIC_LOCAL_INVALID) {""")
patch("src/frontend/parser_expression.c",
      """        function_id = minic_parser_find_function(parser, name_span);
        if (!minic_parser_advance(parser)) {""",
      """        function_id = has_nearest_binding
            ? MINIC_FUNCTION_INVALID : minic_parser_find_function(parser, name_span);
        if (!minic_parser_advance(parser)) {""")
patch("src/frontend/parser_expression.c",
      """        global_object_id = minic_parser_find_global_object(parser, name_span);
        fixed_register_binding_id = minic_parser_find_fixed_register_binding(parser, name_span);
        enumerator_id = minic_parser_find_enum_constant(parser, name_span);""",
      """        global_object_id = has_nearest_binding
            ? nearest_binding.global_object_id
            : minic_parser_find_global_object(parser, name_span);
        fixed_register_binding_id = has_nearest_binding
            ? MINIC_FIXED_REGISTER_BINDING_INVALID
            : minic_parser_find_fixed_register_binding(parser, name_span);
        enumerator_id = has_nearest_binding
            ? nearest_binding.enumerator_id
            : minic_parser_find_enum_constant(parser, name_span);""")

print("MINIC_PARSER_NEAREST_ORDINARY_BINDING_AUDIT_V1=APPLIED")
