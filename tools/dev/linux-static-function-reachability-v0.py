#!/usr/bin/env python3
from pathlib import Path
import re


def sub(path: str, pattern: str, repl: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text()
    new, n = re.subn(pattern, repl, text, count=count, flags=re.M | re.S)
    if n != count:
        raise SystemExit(f"{path}: pattern count {n}, expected {count}: {pattern[:80]}")
    p.write_text(new)


# Persist GNU used/__used__ as a real emission semantic, rather than treating it
# as an optimization-only parse no-op.
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

# Thread the semantic bit through function attribute collection.
sub(
    "src/frontend/parser_function.c",
    r"(    bool \*is_noreturn;\n)(    MinicFunctionId \*alias_target;)",
    r"\1    bool *force_emit;\n\2",
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

# Generic function-attribute parsing has no entity to persist into; make that
# explicit. Entity-routing helpers below pass the real bool storage.
sub(
    "src/frontend/parser_function.c",
    r"(    context\.is_noreturn = NULL;\n)(    context\.alias_target = NULL;)",
    r"\1    context.force_emit = NULL;\n\2",
)

# apply_function_attribute_list: add force_emit output and wire context.
sub(
    "src/frontend/parser_function.c",
    r"(static bool apply_function_attribute_list\(MinicParser \*parser,.*?"
    r"                                          bool \*is_noreturn,\n)("
    r"                                          MinicFunctionId \*alias_target,)",
    r"\1                                          bool *force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(    context\.is_noreturn = is_noreturn;\n)(    context\.alias_target = alias_target;)",
    r"\1    context.force_emit = force_emit;\n\2",
    count=1,
)

# parse_persistent_function_attributes: add force_emit and wire context.
sub(
    "src/frontend/parser_function.c",
    r"(static bool parse_persistent_function_attributes\(MinicParser \*parser,.*?"
    r"                                                 bool \*is_noreturn,\n)("
    r"                                                 MinicFunctionId \*alias_target\) \{)",
    r"\1                                                 bool *force_emit,\n\2",
)
# This is the second entity context initialization (the first was apply_*).
text_path = Path("src/frontend/parser_function.c")
text = text_path.read_text()
needle = "    context.is_noreturn = is_noreturn;\n    context.alias_target = alias_target;"
positions = [m.start() for m in re.finditer(re.escape(needle), text)]
if len(positions) != 1:
    raise SystemExit(f"parser_function.c: expected one remaining persistent context anchor, got {len(positions)}")
text = text.replace(
    needle,
    "    context.is_noreturn = is_noreturn;\n    context.force_emit = force_emit;\n    context.alias_target = alias_target;",
    1,
)
text_path.write_text(text)

# Declarations must preserve used across redeclarations before a later definition.
sub(
    "src/frontend/parser_function.c",
    r"(static bool record_function_declaration_entity\(MinicParser \*parser,.*?"
    r"                                               bool is_noreturn,\n)("
    r"                                               bool is_weak,)",
    r"\1                                               bool force_emit,\n\2",
)
sub(
    "src/frontend/parser_function.c",
    r"(    if \(is_noreturn &&\n        !minic_c0_program_set_function_noreturn\(parser->program, function_id, true\)\) \{.*?\n    \}\n)(    if \(is_weak && !minic_c0_program_set_function_weak)",
    r"\1    if (force_emit &&\n"
    r"        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {\n"
    r"        minic_parser_error(parser, \"cannot persist GNU used function metadata\");\n"
    r"        return false;\n"
    r"    }\n\2",
    count=1,
)
sub(
    "src/frontend/parser_function.c",
    r"(static bool finish_function_declaration_entity\(MinicParser \*parser,.*?"
    r"                                               bool is_noreturn,\n)("
    r"                                               bool is_weak,)",
    r"\1                                               bool force_emit,\n\2",
)
# pass force_emit from finish -> record
sub(
    "src/frontend/parser_function.c",
    r"(                                            is_internal,\n                                            is_noreturn,\n)(                                            is_weak,)",
    r"\1                                            force_emit,\n\2",
    count=1,
)

# parse_function owns the accumulated semantic bit.
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

# All entity-routing apply_function_attribute_list calls currently terminate in
# alias_target. Insert force_emit immediately before that argument.
p = Path("src/frontend/parser_function.c")
text = p.read_text()
old = "            &is_noreturn,\n            &alias_target,"
count = text.count(old)
if count == 0:
    raise SystemExit("parser_function.c: no apply_function_attribute_list entity anchors")
text = text.replace(old, "            &is_noreturn,\n            &force_emit,\n            &alias_target,")
p.write_text(text)

# Persistent declarator attributes.
sub(
    "src/frontend/parser_function.c",
    r"(                                              &is_weak,\n                                              &is_noreturn,\n)(                                              &alias_target\)\) \{)",
    r"\1                                              &force_emit,\n\2",
)

# Semicolon declaration call.
sub(
    "src/frontend/parser_function.c",
    r"(                                                  is_internal,\n                                                  is_noreturn,\n)(                                                  is_weak,)",
    r"\1                                                  force_emit,\n\2",
    count=1,
)

# Function definition: persist the attribute on the canonical entity.
sub(
    "src/frontend/parser_function.c",
    r"(    if \(is_noreturn &&\n        !minic_c0_program_set_function_noreturn\(parser->program, function_id, true\)\) \{.*?\n    \}\n)(    if \(is_weak && !minic_c0_program_set_function_weak)",
    r"\1    if (force_emit &&\n"
    r"        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {\n"
    r"        minic_parser_error(parser, \"cannot persist GNU used function metadata\");\n"
    r"        return false;\n"
    r"    }\n\2",
    count=1,
)

# Broaden semantic reachability from internal-inline-only to every internal
# definition. External definitions remain roots; GNU used is an explicit root.
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

# The lowering gate must now honor the reachability result for every internal
# function, not just inline definitions.
sub(
    "src/compiler/compiler.c",
    r"if \(function->is_internal && function->is_inline && !function->is_referenced\)",
    r"if (function->is_internal && !function->is_referenced)",
)

print("LINUX_STATIC_FUNCTION_REACHABILITY_PATCH=APPLIED")
