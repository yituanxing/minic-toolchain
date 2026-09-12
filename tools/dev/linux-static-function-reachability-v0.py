#!/usr/bin/env python3
from pathlib import Path
import re


def sub(path: str, pattern: str, repl: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text()
    new, n = re.subn(pattern, repl, text, count=count, flags=re.M | re.S)
    if n != count:
        raise SystemExit(f"{path}: pattern count {n}, expected {count}: {pattern[:100]}")
    p.write_text(new)


# Persist GNU used/__used__ as a real force-emission semantic.
sub(
    "src/frontend/ast.h",
    r"(    bool is_referenced;\n)(    bool is_variadic;)",
    r"\1    bool force_emit;\n\2",
)
sub(
    "src/frontend/ast.h",
    r"(bool minic_c0_program_set_function_referenced\(MinicC0Program \*program,\n"
    r"                                              MinicFunctionId function_id,\n"
    r"                                              bool is_referenced\);\n)",
    r"\1bool minic_c0_program_set_function_force_emit(MinicC0Program *program,\n"
    r"                                              MinicFunctionId function_id,\n"
    r"                                              bool force_emit);\n",
)
sub(
    "src/frontend/ast_function.c",
    r"(bool minic_c0_program_set_function_referenced\(MinicC0Program \*program,.*?\n}\n\n)(bool minic_c0_program_set_function_weak)",
    r"\1bool minic_c0_program_set_function_force_emit(MinicC0Program *program,\n"
    r"                                              MinicFunctionId function_id,\n"
    r"                                              bool force_emit) {\n"
    r"    if (program == NULL || function_id >= program->function_count) {\n"
    r"        return false;\n"
    r"    }\n"
    r"    if (force_emit) {\n"
    r"        program->functions[function_id].force_emit = true;\n"
    r"    }\n"
    r"    return true;\n"
    r"}\n\n\2",
)

# Attribute parsing: ordinary collected lists are scanned locally; only the
# post-declarator persistent parser needs an output pointer.
sub(
    "src/frontend/parser_function.c",
    r"(    bool \*is_noreturn;\n)(    MinicFunctionId \*alias_target;)",
    r"\1    bool *force_emit;\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(static bool function_attribute_class_is_parse_only\(MinicAttributeClass semantic_class\) \{.*?\n}\n)",
    r"\1\nstatic bool function_attribute_list_has_used(const MinicParsedAttributeList *attributes) {\n"
    r"    size_t index;\n\n"
    r"    if (attributes == NULL) {\n"
    r"        return false;\n"
    r"    }\n"
    r"    for (index = 0U; index < attributes->count; ++index) {\n"
    r"        const MinicAttributeDescriptor *descriptor = attributes->values[index].descriptor;\n"
    r"        if (descriptor != NULL && descriptor->kind == MINIC_ATTRIBUTE_USED) {\n"
    r"            return true;\n"
    r"        }\n"
    r"    }\n"
    r"    return false;\n"
    r"}\n",
)
sub(
    "src/frontend/parser_function.c",
    r"(    if \(descriptor->kind == MINIC_ATTRIBUTE_NORETURN\) \{.*?\n    \}\n\n)(    if \(descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE\))",
    r"\1    if (descriptor->kind == MINIC_ATTRIBUTE_USED) {\n"
    r"        if (context->force_emit != NULL) {\n"
    r"            *context->force_emit = true;\n"
    r"        }\n"
    r"        return true;\n"
    r"    }\n\n\2",
)

# Three context initializers: generic parse, collected-list apply, persistent parse.
sub(
    "src/frontend/parser_function.c",
    r"(    context\.is_noreturn = NULL;\n)(    context\.alias_target = NULL;)",
    r"\1    context.force_emit = NULL;\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(static bool apply_function_attribute_list\(.*?    context\.is_noreturn = is_noreturn;\n)(    context\.alias_target = alias_target;)",
    r"\1    context.force_emit = NULL;\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(static bool parse_persistent_function_attributes\(MinicParser \*parser,.*?"
    r"                                                 bool \*is_noreturn,\n)("
    r"                                                 MinicFunctionId \*alias_target\) \{)",
    r"\1                                                 bool *force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(static bool parse_persistent_function_attributes\(.*?    context\.is_noreturn = is_noreturn;\n)(    context\.alias_target = alias_target;)",
    r"\1    context.force_emit = force_emit;\n\2",
)

# finish_function_declaration_entity gets one semantic bit; the lower-level
# entity recorder remains unchanged.
sub(
    "src/frontend/parser_function.c",
    r"(static bool finish_function_declaration_entity\(MinicParser \*parser,.*?"
    r"                                               bool has_section,\n)("
    r"                                               MinicFunctionId alias_target\) \{)",
    r"\1                                               bool force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(    function_id = minic_parser_find_function\(parser, name_span\);\n"
    r"    if \(function_id == MINIC_FUNCTION_INVALID\) \{\n"
    r"        return false;\n"
    r"    \}\n)(    if \(alias_target != MINIC_FUNCTION_INVALID\))",
    r'\1    if (force_emit &&\n'
    r'        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {\n'
    r'        minic_parser_error(parser, "cannot persist GNU used function metadata");\n'
    r'        return false;\n'
    r'    }\n\2',
)

# parse_function owns the declaration-wide bit. Prefix/declarator lists are
# already retained in memory, so inspect them directly instead of threading a
# pointer through every parser helper.
sub(
    "src/frontend/parser_function.c",
    r"(    bool is_noreturn;\n)(    bool is_register_declaration;)",
    r"\1    bool force_emit;\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(    is_noreturn = false;\n)(    is_register_declaration = false;)",
    r"\1    force_emit = false;\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(    } else \{\n        minic_parser_error\(parser, \"expected function or extern object name\"\);\n"
    r"        return false;\n    \}\n)(\n    if \(is_register_declaration\))",
    r"\1\n    force_emit = function_attribute_list_has_used(&deferred_attributes) ||\n"
    r"                 function_attribute_list_has_used(&declarator_attributes);\n\2",
)

# Function-typed declarations: merge persistent used into the entity bit and
# persist after the canonical declaration entity exists.
sub(
    "src/frontend/parser_function.c",
    r"(        for \(;;\) \{\n            bool entity_is_weak;\n            bool entity_is_noreturn;\n)",
    r"\1            bool entity_force_emit;\n",
)
sub(
    "src/frontend/parser_function.c",
    r"(            entity_is_weak = declaration_is_weak;\n"
    r"            entity_is_noreturn = is_noreturn;\n)",
    r"\1            entity_force_emit = force_emit;\n",
)
sub(
    "src/frontend/parser_function.c",
    r"(                    &entity_is_weak,\n                    &entity_is_noreturn,\n)(                    NULL\)\) \{)",
    r"\1                    &entity_force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(            if \(!record_function_declaration_entity\(parser,.*?"
    r"                                                    has_section\)\) \{\n"
    r"                return false;\n"
    r"            \}\n)(            if \(parser->current.kind == MINIC_TOKEN_SEMICOLON\))",
    r'\1            function_id = minic_parser_find_function(parser, name_span);\n'
    r'            if (function_id == MINIC_FUNCTION_INVALID ||\n'
    r'                (entity_force_emit &&\n'
    r'                 !minic_c0_program_set_function_force_emit(\n'
    r'                     parser->program, function_id, true))) {\n'
    r'                minic_parser_error(parser, "cannot persist GNU used function metadata");\n'
    r'                return false;\n'
    r'            }\n\2',
)

# Ordinary function persistent attributes and semicolon declaration.
sub(
    "src/frontend/parser_function.c",
    r"(                                              &is_weak,\n                                              &is_noreturn,\n)(                                              &alias_target\)\) \{)",
    r"\1                                              &force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(                                                  section_name_length,\n                                                  has_section,\n)(                                                  alias_target\);)",
    r"\1                                                  force_emit,\n\2",
)

# Definition path: persist used on the canonical entity before lowering.
sub(
    "src/frontend/parser_function.c",
    r"(    if \(!minic_c0_program_set_function_inline\(parser->program, function_id, is_inline\)\) \{.*?\n    \}\n)(    if \(is_noreturn &&)",
    r'\1    if (force_emit &&\n'
    r'        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {\n'
    r'        minic_parser_error(parser, "cannot persist GNU used function metadata");\n'
    r'        return false;\n'
    r'    }\n\2',
)

# Broaden semantic reachability from internal-inline-only to all internal
# definitions. External definitions remain roots; GNU used is an explicit root.
sub(
    "src/frontend/function_body.c",
    r"        if \(function->is_defined && !\(function->is_internal && function->is_inline\) &&\n"
    r"            !enqueue_emission_function\(",
    r"        if (function->is_defined && (!function->is_internal || function->force_emit) &&\n"
    r"            !enqueue_emission_function(",
)
sub(
    "src/frontend/function_body.c",
    r"        if \(function->is_defined && function->is_internal && function->is_inline\) \{\n"
    r"            function->is_referenced = reachable\[function_index\];\n"
    r"        \}",
    r"        if (function->is_defined && function->is_internal) {\n"
    r"            function->is_referenced = reachable[function_index];\n"
    r"        }",
)
sub(
    "src/compiler/compiler.c",
    r"if \(function->is_internal && function->is_inline && !function->is_referenced\)",
    r"if (function->is_internal && !function->is_referenced)",
)

print("LINUX_STATIC_FUNCTION_REACHABILITY_PATCH=APPLIED")
