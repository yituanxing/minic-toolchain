#!/usr/bin/env python3
from pathlib import Path

# Perf-only parser lookup counters.  This patch changes no lookup decisions; it
# only counts calls/candidate iterations while MINIC_PARSER_PERF_TRACE is used.

p = Path("src/frontend/parser_internal.h")
text = p.read_text()
anchor = "    size_t enum_tag_capacity;\n} MinicParser;\n"
fields = """    size_t enum_tag_capacity;

    size_t perf_find_local_calls;
    size_t perf_find_local_steps;
    size_t perf_find_function_calls;
    size_t perf_find_function_steps;
    size_t perf_find_global_calls;
    size_t perf_find_global_steps;
    size_t perf_find_type_alias_calls;
    size_t perf_find_type_alias_steps;
    size_t perf_find_enum_calls;
    size_t perf_find_enum_steps;
    size_t perf_find_fixed_calls;
    size_t perf_find_fixed_steps;
    size_t perf_builtin_mem_calls;
    size_t perf_builtin_mem_steps;
} MinicParser;
"""
if text.count(anchor) != 1:
    raise SystemExit(f"parser struct anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor, fields, 1))

# Local/function lookups.
p = Path("src/frontend/parser_core.c")
text = p.read_text()
old = """MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    for (index = parser->local_binding_count; index > 0U; --index) {
"""
new = """MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;
    MinicParser *perf_parser = (MinicParser *)parser;

    perf_parser->perf_find_local_calls += 1U;
    for (index = parser->local_binding_count; index > 0U; --index) {
        perf_parser->perf_find_local_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"find-local anchor count={text.count(old)}")
text = text.replace(old,new,1)

old = """MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = parser->program->function_count; index > 0U; --index) {
"""
new = """MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;
    MinicParser *perf_parser = (MinicParser *)parser;

    perf_parser->perf_find_function_calls += 1U;
    name_length = minic_parser_span_length(name_span);
    for (index = parser->program->function_count; index > 0U; --index) {
        perf_parser->perf_find_function_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"find-function anchor count={text.count(old)}")
p.write_text(text.replace(old,new,1))

# Global/fixed-register lookups.
p = Path("src/frontend/parser_global.c")
text = p.read_text()
old = """    name_length = minic_parser_span_length(name_span);
    for (index = parser->program->global_object_count; index > 0U; --index) {
        size_t object_index = index - 1U;
"""
new = """    ((MinicParser *)parser)->perf_find_global_calls += 1U;
    name_length = minic_parser_span_length(name_span);
    for (index = parser->program->global_object_count; index > 0U; --index) {
        size_t object_index = index - 1U;
        ((MinicParser *)parser)->perf_find_global_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"global lookup anchor count={text.count(old)}")
text = text.replace(old,new,1)

old = """    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;
"""
new = """    ((MinicParser *)parser)->perf_find_fixed_calls += 1U;
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;
        ((MinicParser *)parser)->perf_find_fixed_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"fixed lookup anchor count={text.count(old)}")
p.write_text(text.replace(old,new,1))

# Typedef lookup: count both scoped-binding and global-alias iterations.
p = Path("src/frontend/parser_typedef.c")
text = p.read_text()
old = """    if (parser == NULL) {
        return MINIC_TYPE_ALIAS_INVALID;
    }
    for (index = parser->local_binding_count; index > 0U; --index) {
        const MinicParserLocalBinding *binding;
"""
new = """    if (parser == NULL) {
        return MINIC_TYPE_ALIAS_INVALID;
    }
    ((MinicParser *)parser)->perf_find_type_alias_calls += 1U;
    for (index = parser->local_binding_count; index > 0U; --index) {
        const MinicParserLocalBinding *binding;
        ((MinicParser *)parser)->perf_find_type_alias_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"typedef scoped anchor count={text.count(old)}")
text = text.replace(old,new,1)
old = """    for (index = 0U; index < parser->program->type_alias_count; ++index) {
        const MinicTypeAlias *alias;
"""
new = """    for (index = 0U; index < parser->program->type_alias_count; ++index) {
        const MinicTypeAlias *alias;
        ((MinicParser *)parser)->perf_find_type_alias_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"typedef global anchor count={text.count(old)}")
p.write_text(text.replace(old,new,1))

# Enumerator lookup.
p = Path("src/frontend/parser_enum.c")
text = p.read_text()
old = """    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];
"""
new = """    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    ((MinicParser *)parser)->perf_find_enum_calls += 1U;
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];
        ((MinicParser *)parser)->perf_find_enum_steps += 1U;
"""
if text.count(old) != 1:
    raise SystemExit(f"enum lookup anchor count={text.count(old)}")
p.write_text(text.replace(old,new,1))

# Builtin memcpy/memmove/memset canonical-callee scans.  There are exactly
# three identical loop shapes in parser_expression.c.
p = Path("src/frontend/parser_expression.c")
text = p.read_text()
loop = """    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        function = minic_c0_program_function(parser->program, index);
"""
repl = """    parser->perf_builtin_mem_calls += 1U;
    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        parser->perf_builtin_mem_steps += 1U;
        function = minic_c0_program_function(parser->program, index);
"""
count=text.count(loop)
if count != 3:
    raise SystemExit(f"builtin mem loop count={count}")
text=text.replace(loop,repl)
p.write_text(text)

# Emit one aggregate line at the end of parsing.
p = Path("src/frontend/parser_function.c")
text = p.read_text()
anchor = """    if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {
        minic_parser_error(&parser, "parser failed without diagnostic");
    }
    minic_parser_destroy_scopes(&parser);
"""
new = """    if (!success && diagnostic != NULL && diagnostic->message[0] == '\\0') {
        minic_parser_error(&parser, "parser failed without diagnostic");
    }
    if (getenv("MINIC_PARSER_PERF_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_PARSER_LOOKUPS local=%zu/%zu function=%zu/%zu "
                      "global=%zu/%zu typedef=%zu/%zu enum=%zu/%zu "
                      "fixed=%zu/%zu builtin_mem=%zu/%zu "
                      "functions=%zu globals=%zu aliases=%zu expressions=%zu\\n",
                      parser.perf_find_local_calls, parser.perf_find_local_steps,
                      parser.perf_find_function_calls, parser.perf_find_function_steps,
                      parser.perf_find_global_calls, parser.perf_find_global_steps,
                      parser.perf_find_type_alias_calls, parser.perf_find_type_alias_steps,
                      parser.perf_find_enum_calls, parser.perf_find_enum_steps,
                      parser.perf_find_fixed_calls, parser.perf_find_fixed_steps,
                      parser.perf_builtin_mem_calls, parser.perf_builtin_mem_steps,
                      program->function_count, program->global_object_count,
                      program->type_alias_count, program->expression_count);
    }
    minic_parser_destroy_scopes(&parser);
"""
if text.count(anchor) != 1:
    raise SystemExit(f"parser summary anchor count={text.count(anchor)}")
p.write_text(text.replace(anchor,new,1))

print("MINIC_PERF_PARSER_LOOKUP_COUNTERS_V0=APPLIED")
