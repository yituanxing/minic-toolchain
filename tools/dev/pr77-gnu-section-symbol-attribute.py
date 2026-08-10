#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Program-owned symbol metadata.
# ---------------------------------------------------------------------------
replace_once(
    "src/frontend/ast.h",
    """    char *assembler_name;
    size_t assembler_name_length;
    MinicSymbolVisibility visibility;
""",
    """    char *assembler_name;
    size_t assembler_name_length;
    char *section_name;
    size_t section_name_length;
    MinicSymbolVisibility visibility;
""",
    "function-section-fields",
)
replace_once(
    "src/frontend/ast.h",
    """typedef struct MinicGlobalObject {
    char *name;
    size_t name_length;
    MinicType type;
""",
    """typedef struct MinicGlobalObject {
    char *name;
    size_t name_length;
    char *section_name;
    size_t section_name_length;
    MinicType type;
""",
    "object-section-fields",
)
replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_set_function_visibility(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              MinicSymbolVisibility visibility);
""",
    """bool minic_c0_program_set_function_visibility(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              MinicSymbolVisibility visibility);
bool minic_c0_program_set_function_section(MinicC0Program *program,
                                           MinicFunctionId function_id,
                                           const char *name,
                                           size_t name_length);
""",
    "function-section-prototype",
)
replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_global_object_set_visibility(MinicC0Program *program,
                                           MinicGlobalObjectId global_object_id,
                                           MinicSymbolVisibility visibility);
""",
    """bool minic_c0_global_object_set_visibility(MinicC0Program *program,
                                           MinicGlobalObjectId global_object_id,
                                           MinicSymbolVisibility visibility);
bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length);
""",
    "object-section-prototype",
)

# Section strings are owned by Program symbols, like assembler names.
replace_once(
    "src/frontend/ast.c",
    """    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);
        free(program->functions[index].assembler_name);
    }
""",
    """    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);
        free(program->functions[index].assembler_name);
        free(program->functions[index].section_name);
    }
""",
    "function-section-destroy",
)
replace_once(
    "src/frontend/ast.c",
    """    for (index = 0U; index < program->global_object_count; ++index) {
        free(program->global_objects[index].name);
        free(program->global_objects[index].initializer_values);
""",
    """    for (index = 0U; index < program->global_object_count; ++index) {
        free(program->global_objects[index].name);
        free(program->global_objects[index].section_name);
        free(program->global_objects[index].initializer_values);
""",
    "object-section-destroy",
)

ast_function = Path("src/frontend/ast_function.c")
text = ast_function.read_text()
text += r'''

bool minic_c0_program_set_function_section(MinicC0Program *program,
                                           MinicFunctionId function_id,
                                           const char *name,
                                           size_t name_length) {
    MinicFunction *function;
    char *copy;

    if (program == NULL || function_id >= program->function_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->section_name != NULL) {
        return function->section_name_length == name_length &&
               memcmp(function->section_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    function->section_name = copy;
    function->section_name_length = name_length;
    return true;
}
'''
ast_function.write_text(text)

ast_global = Path("src/frontend/ast_global.c")
text = ast_global.read_text()
text += r'''

bool minic_c0_global_object_set_section(MinicC0Program *program,
                                        MinicGlobalObjectId global_object_id,
                                        const char *name,
                                        size_t name_length) {
    MinicGlobalObject *object;
    char *copy;

    if (program == NULL || global_object_id >= program->global_object_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->section_name != NULL) {
        return object->section_name_length == name_length &&
               memcmp(object->section_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    object->section_name = copy;
    object->section_name_length = name_length;
    return true;
}
'''
ast_global.write_text(text)

# ---------------------------------------------------------------------------
# Shared selective parser for __attribute__((section("..."))). It consumes only
# section wrappers; unrelated attributes are intentionally left for the common
# GNU attribute parser so `section` does not become a greedy special case.
# ---------------------------------------------------------------------------
path = Path("src/frontend/parser_internal.h")
text = path.read_text()
anchor = """bool minic_parser_parse_gnu_function_attributes(MinicParser *parser);
"""
prototype = """bool minic_parser_parse_gnu_section_attribute(MinicParser *parser,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *length,
                                              bool *has_section);
"""
if text.count(anchor) != 1:
    raise SystemExit(f"section parser prototype: expected one shared GNU attribute anchor, found {text.count(anchor)}")
path.write_text(text.replace(anchor, anchor + prototype, 1))

path = Path("src/frontend/parser_function.c")
text = path.read_text()
anchor = """bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {
"""
helper = r'''static bool section_attribute_token_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind == MINIC_TOKEN_EOF) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

bool minic_parser_parse_gnu_section_attribute(MinicParser *parser,
                                              char *buffer,
                                              size_t capacity,
                                              size_t *length,
                                              bool *has_section) {
    for (;;) {
        MinicParser probe;
        char parsed[256];
        size_t parsed_length;

        if (parser == NULL || buffer == NULL || length == NULL || has_section == NULL ||
            capacity == 0U) {
            return false;
        }
        if (!section_attribute_token_is(parser, "__attribute__")) {
            return true;
        }

        probe = *parser;
        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe)) {
            return false;
        }
        if (!section_attribute_token_is(&probe, "section") &&
            !section_attribute_token_is(&probe, "__section__")) {
            return true;
        }

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__") ||
            (!section_attribute_token_is(parser, "section") &&
             !section_attribute_token_is(parser, "__section__")) ||
            !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after section")) {
            return false;
        }

        parsed_length = 0U;
        if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
            minic_parser_error(parser, "GNU section attribute requires a string literal");
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            size_t cursor;
            size_t end;

            if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
                minic_parser_error(parser, "invalid GNU section string");
                return false;
            }
            cursor = parser->current.span.begin.offset + 1U;
            end = parser->current.span.end.offset - 1U;
            while (cursor < end) {
                if (parser->source[cursor] == '\\') {
                    minic_parser_error(parser, "escaped GNU section names are not supported yet");
                    return false;
                }
                if (parsed_length + 1U >= sizeof(parsed)) {
                    minic_parser_error(parser, "GNU section name is too long");
                    return false;
                }
                parsed[parsed_length++] = parser->source[cursor++];
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
        if (parsed_length == 0U || parsed_length + 1U > capacity ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after section name") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU section attribute") ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_RPAREN,
                                 "expected second ')' in GNU section attribute")) {
            return false;
        }
        parsed[parsed_length] = '\0';
        if (*has_section) {
            if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
                minic_parser_error(parser, "conflicting GNU section attributes");
                return false;
            }
        } else {
            (void)memcpy(buffer, parsed, parsed_length + 1U);
            *length = parsed_length;
            *has_section = true;
        }
    }
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"section parser helper: expected one GNU function attribute definition, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

# Function declarations/definitions: collect section after the return type and
# before the generic attribute consumer, then attach it to the Program-owned
# function symbol so redeclarations and later definitions inherit it.
old = """    bool has_assembler_name;
    MinicSymbolVisibility visibility;
    bool has_visibility;
"""
new = """    bool has_assembler_name;
    char section_name[256];
    size_t section_name_length;
    bool has_section;
    MinicSymbolVisibility visibility;
    bool has_visibility;
"""
if text.count(old) != 1:
    raise SystemExit(f"function section locals: expected one visibility local block, found {text.count(old)}")
text = text.replace(old, new, 1)
old = """    has_assembler_name = false;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    has_visibility = false;
    (void)memset(assembler_name, 0, sizeof(assembler_name));
"""
new = """    has_assembler_name = false;
    section_name_length = 0U;
    has_section = false;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    has_visibility = false;
    (void)memset(assembler_name, 0, sizeof(assembler_name));
    (void)memset(section_name, 0, sizeof(section_name));
"""
if text.count(old) != 1:
    raise SystemExit(f"function section init: expected one visibility init block, found {text.count(old)}")
text = text.replace(old, new, 1)
old = """    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (!parse_gnu_predeclarator_function_attributes(parser)) {
"""
new = """    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (!minic_parser_parse_gnu_section_attribute(parser,
                                                   section_name,
                                                   sizeof(section_name),
                                                   &section_name_length,
                                                   &has_section) ||
        !parse_gnu_predeclarator_function_attributes(parser)) {
"""
if text.count(old) != 1:
    raise SystemExit(f"function section parse: expected one predeclarator attribute call, found {text.count(old)}")
text = text.replace(old, new, 1)

# Declaration metadata is applied before the semicolon is consumed.
old = """        if (has_visibility &&
            !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
            minic_parser_error(parser, "conflicting GNU function visibility");
            return false;
        }
        return minic_parser_advance(parser);
"""
new = """        if (has_visibility &&
            !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
            minic_parser_error(parser, "conflicting GNU function visibility");
            return false;
        }
        if (has_section &&
            !minic_c0_program_set_function_section(
                parser->program, function_id, section_name, section_name_length)) {
            minic_parser_error(parser, "conflicting or invalid GNU function section");
            return false;
        }
        return minic_parser_advance(parser);
"""
if text.count(old) != 1:
    raise SystemExit(f"function declaration section attach: expected one visibility declaration block, found {text.count(old)}")
text = text.replace(old, new, 1)
old = """    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    parser->current_function = function_id;
"""
new = """    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    if (has_section &&
        !minic_c0_program_set_function_section(
            parser->program, function_id, section_name, section_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function section");
        return false;
    }
    parser->current_function = function_id;
"""
if text.count(old) != 1:
    raise SystemExit(f"function definition section attach: expected one visibility definition block, found {text.count(old)}")
text = text.replace(old, new, 1)

# The extern function/object classifier must skip a section attribute without
# consuming the real parser. Otherwise `extern char section(...) object[]` is
# misrouted into the function parser before the object parser can see it.
start_marker = "static bool extern_declaration_is_function(MinicParser *parser, bool *is_function) {"
start = text.find(start_marker)
if start < 0:
    raise SystemExit("extern section probe: missing extern_declaration_is_function")
end_candidates = [p for p in (text.find("\nstatic ", start + 1), text.find("\nbool ", start + 1)) if p >= 0]
end = min(end_candidates) if end_candidates else len(text)
body = text[start:end]
old = """    MinicType base_type;
    MinicType declared_type;
"""
new = """    MinicType base_type;
    MinicType declared_type;
    char section_name[256];
    size_t section_name_length;
    bool has_section;
"""
if body.count(old) != 1:
    raise SystemExit(f"extern section probe locals: expected one type local block, found {body.count(old)}")
body = body.replace(old, new, 1)
old = """    probe = *parser;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_type_specifiers(&probe, &base_type) ||
        !minic_parser_parse_pointer_declarator(&probe, base_type, &declared_type)) {
"""
new = """    probe = *parser;
    section_name_length = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_type_specifiers(&probe, &base_type) ||
        !minic_parser_parse_pointer_declarator(&probe, base_type, &declared_type) ||
        !minic_parser_parse_gnu_section_attribute(&probe,
                                                   section_name,
                                                   sizeof(section_name),
                                                   &section_name_length,
                                                   &has_section)) {
"""
if body.count(old) != 1:
    raise SystemExit(f"extern section probe parse: expected one relaxed type probe, found {body.count(old)}")
body = body.replace(old, new, 1)
text = text[:start] + body + text[end:]
path.write_text(text)

# Extern objects share one declaration-level section attribute across a comma
# declarator list and keep it on every Program-owned symbol.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
start_marker = "bool minic_parser_parse_extern_global(MinicParser *parser) {"
end_marker = "static bool\nparse_static_pointer_array"
start = text.find(start_marker)
end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
if start < 0 or end < 0:
    raise SystemExit("extern object section: cannot locate parse_extern_global")
body = text[start:end]
old = """    MinicType base_type;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
"""
new = """    MinicType base_type;
    char section_name[256];
    size_t section_name_length;
    bool has_section;

    section_name_length = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(parser,
                                                   section_name,
                                                   sizeof(section_name),
                                                   &section_name_length,
                                                   &has_section)) {
        return false;
    }

    for (;;) {
"""
if body.count(old) != 1:
    raise SystemExit(f"extern object section parse: expected one declarator-list prologue, found {body.count(old)}")
body = body.replace(old, new, 1)
old = """            !minic_c0_global_object_set_extern(parser->program, object_id)) {
"""
new = """            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (has_section &&
             !minic_c0_global_object_set_section(
                 parser->program, object_id, section_name, section_name_length))) {
"""
if body.count(old) != 1:
    raise SystemExit(f"extern object section attach: expected one extern setter, found {body.count(old)}")
body = body.replace(old, new, 1)
path.write_text(text[:start] + body + text[end:])

# ---------------------------------------------------------------------------
# RV64 emission. Section is symbol placement semantics, so definitions switch to
# the named section instead of treating the attribute as diagnostic metadata.
# ---------------------------------------------------------------------------
path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
old = """    if (fprintf(file, "%s\\n", object->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
"""
new = """    if (object->section_name != NULL) {
        if (fprintf(file, ".section %s\\n", object->section_name) < 0) {
            return false;
        }
    } else if (fprintf(file, "%s\\n", object->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
"""
if text.count(old) != 1:
    raise SystemExit(f"object section codegen: expected one default data section emission, found {text.count(old)}")
text = text.replace(old, new, 1)
old = """    success = true;
    if (!function->is_internal) {
"""
new = """    success = function->section_name != NULL
                  ? fprintf(file, ".section %s\\n", function->section_name) >= 0
                  : fprintf(file, ".text\\n") >= 0;
    if (success && !function->is_internal) {
"""
if text.count(old) != 1:
    raise SystemExit(f"function section codegen: expected one function success prologue, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged GNU section symbol placement for extern objects, functions and RV64 definitions")
