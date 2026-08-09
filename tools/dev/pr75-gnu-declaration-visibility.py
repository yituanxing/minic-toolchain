#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    "typedef struct MinicFunction {\n",
    "typedef enum MinicSymbolVisibility {\n"
    "    MINIC_SYMBOL_VISIBILITY_DEFAULT = 0,\n"
    "    MINIC_SYMBOL_VISIBILITY_HIDDEN,\n"
    "    MINIC_SYMBOL_VISIBILITY_INTERNAL,\n"
    "    MINIC_SYMBOL_VISIBILITY_PROTECTED\n"
    "} MinicSymbolVisibility;\n\n"
    "typedef struct MinicFunction {\n",
)
replace_once(
    "src/frontend/ast.h",
    "    size_t assembler_name_length;\n    MinicType return_type;\n",
    "    size_t assembler_name_length;\n    MinicSymbolVisibility visibility;\n    MinicType return_type;\n",
)
replace_once(
    "src/frontend/ast.h",
    "const char *minic_c0_function_symbol_name(const MinicFunction *function);\n",
    "const char *minic_c0_function_symbol_name(const MinicFunction *function);\n"
    "bool minic_c0_program_set_function_visibility(MinicC0Program *program,\n"
    "                                              MinicFunctionId function_id,\n"
    "                                              MinicSymbolVisibility visibility);\n",
)

ast_function = Path("src/frontend/ast_function.c")
text = ast_function.read_text()
text += r'''

bool minic_c0_program_set_function_visibility(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              MinicSymbolVisibility visibility) {
    MinicFunction *function;

    if (program == NULL || function_id >= program->function_count ||
        visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
        function->visibility != visibility) {
        return false;
    }
    function->visibility = visibility;
    return true;
}
'''
ast_function.write_text(text)

parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
anchor = "static bool function_signature_matches(const MinicFunction *function,\n"
helper = r'''static bool parse_gnu_visibility_name(MinicParser *parser,
                                      MinicSymbolVisibility *visibility) {
    MinicSourceSpan span;
    const char *value;
    size_t length;

    if (parser == NULL || visibility == NULL || parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    span = parser->current.span;
    if (span.end.offset <= span.begin.offset + 1U) {
        minic_parser_error(parser, "invalid GNU visibility string");
        return false;
    }
    value = parser->source + span.begin.offset + 1U;
    length = span.end.offset - span.begin.offset - 2U;
    if (length == 8U && memcmp(value, "internal", 8U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
    } else if (length == 6U && memcmp(value, "hidden", 6U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    } else if (length == 9U && memcmp(value, "protected", 9U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
    } else if (length == 7U && memcmp(value, "default", 7U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        minic_parser_error(parser, "unsupported GNU visibility value");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_gnu_prefix_function_visibility(MinicParser *parser,
                                                 MinicSymbolVisibility *visibility,
                                                 bool *has_visibility) {
    if (parser == NULL || visibility == NULL || has_visibility == NULL) {
        return false;
    }
    *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    *has_visibility = false;
    while (function_identifier_is(parser, "__attribute__")) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }
        if (!function_identifier_is(parser, "visibility")) {
            minic_parser_error(parser, "unsupported GNU prefix function attribute");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after visibility") ||
            !parse_gnu_visibility_name(parser, visibility) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after visibility") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU attribute") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU attribute")) {
            return false;
        }
        *has_visibility = true;
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"visibility helper anchor: expected 1 match, found {text.count(anchor)}")
parser.write_text(text.replace(anchor, helper + anchor, 1))

replace_once(
    "src/frontend/parser_function.c",
    "    bool has_assembler_name;\n",
    "    bool has_assembler_name;\n    MinicSymbolVisibility visibility;\n    bool has_visibility;\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    has_assembler_name = false;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n",
    "    has_assembler_name = false;\n    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;\n"
    "    has_visibility = false;\n    (void)memset(assembler_name, 0, sizeof(assembler_name));\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    if (is_internal &&\n        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, \"expected keyword 'static'\")) {\n",
    "    if (!parse_gnu_prefix_function_visibility(parser, &visibility, &has_visibility)) {\n"
    "        return false;\n    }\n    if (is_internal &&\n"
    "        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, \"expected keyword 'static'\")) {\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "        if (has_assembler_name &&\n            !minic_c0_program_set_function_assembler_name(\n                parser->program, function_id, assembler_name, assembler_name_length)) {\n            minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n            return false;\n        }\n        return minic_parser_advance(parser);\n",
    "        if (has_assembler_name &&\n            !minic_c0_program_set_function_assembler_name(\n                parser->program, function_id, assembler_name, assembler_name_length)) {\n"
    "            minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n            return false;\n        }\n"
    "        if (has_visibility &&\n            !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {\n"
    "            minic_parser_error(parser, \"conflicting GNU function visibility\");\n            return false;\n        }\n"
    "        return minic_parser_advance(parser);\n",
)
replace_once(
    "src/frontend/parser_function.c",
    "    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n        return false;\n    }\n    parser->current_function = function_id;\n",
    "    if (has_assembler_name &&\n        !minic_c0_program_set_function_assembler_name(\n            parser->program, function_id, assembler_name, assembler_name_length)) {\n"
    "        minic_parser_error(parser, \"conflicting or invalid GNU function asm label\");\n        return false;\n    }\n"
    "    if (has_visibility &&\n        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {\n"
    "        minic_parser_error(parser, \"conflicting GNU function visibility\");\n        return false;\n    }\n"
    "    parser->current_function = function_id;\n",
)

# Preserve ELF visibility on emitted definitions.
codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
old = '''    if (!function->is_internal) {\n        success = fprintf(file, ".globl %s\\n", symbol_name) >= 0;\n    }\n    if (success) {\n'''
new = '''    if (!function->is_internal) {\n        success = fprintf(file, ".globl %s\\n", symbol_name) >= 0;\n        if (success && function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {\n            const char *directive;\n\n            directive = function->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN ? ".hidden"\n                        : function->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL ? ".internal"\n                        : function->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED ? ".protected"\n                                                                                   : NULL;\n            success = directive != NULL && fprintf(file, "%s %s\\n", directive, symbol_name) >= 0;\n        }\n    }\n    if (success) {\n'''
if text.count(old) != 1:
    raise SystemExit(f"visibility codegen anchor: expected 1 match, found {text.count(old)}")
codegen.write_text(text.replace(old, new, 1))

print("staged GNU prefix function visibility with ELF symbol directives")
