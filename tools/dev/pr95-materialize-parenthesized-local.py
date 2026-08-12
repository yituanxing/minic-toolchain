#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n                                                  MinicParsedFunctionDeclarator *declarator);\n''',
    '''bool minic_parser_parse_direct_declarator_name(MinicParser *parser,\n                                               MinicSourceSpan *name_span);\nbool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n                                                  MinicParsedFunctionDeclarator *declarator);\n''',
    "shared direct declarator name declaration",
)

replace_once(
    "src/frontend/parser_declarator.c",
    '''bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n''',
    '''bool minic_parser_parse_direct_declarator_name(MinicParser *parser,\n                                               MinicSourceSpan *name_span) {\n    size_t parenthesis_depth;\n\n    if (parser == NULL || name_span == NULL) {\n        return false;\n    }\n    parenthesis_depth = 0U;\n    while (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        parenthesis_depth += 1U;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n        minic_parser_error(parser, "expected declarator name");\n        return false;\n    }\n    *name_span = parser->current.span;\n    if (!minic_parser_advance(parser)) {\n        return false;\n    }\n    while (parenthesis_depth > 0U) {\n        if (!minic_parser_expect(\n                parser, MINIC_TOKEN_RPAREN, "expected ')' after declarator name")) {\n            return false;\n        }\n        parenthesis_depth -= 1U;\n    }\n    return true;\n}\n\nbool minic_parser_parse_function_parameter_suffix(MinicParser *parser,\n''',
    "shared direct declarator name parser",
)

replace_once(
    "src/frontend/parser_statement.c",
    '''    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n        minic_parser_error(parser, "expected local name");\n        return false;\n    }\n\n    local.name_span = parser->current.span;\n''',
    '''    if (!minic_parser_parse_direct_declarator_name(parser, &local.name_span)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "expected local name");\n        }\n        return false;\n    }\n\n''',
    "local declarator consumes shared direct name",
)

replace_once(
    "src/frontend/parser_statement.c",
    '''    if (!minic_parser_advance(parser)) {\n        return false;\n    }\n    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n''',
    "remove duplicate local-name advance",
)

replace_once(
    "src/frontend/parser_statement.c",
    '''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {\n        name_span = parser->current.span;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n            minic_parser_error(parser, "expected function name in block-scope extern declarator");\n            return false;\n        }\n        name_span = parser->current.span;\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_expect(parser,\n                                 MINIC_TOKEN_RPAREN,\n                                 "expected ')' after block-scope extern function name")) {\n            return false;\n        }\n    } else {\n        minic_parser_error(parser, "expected block-scope extern function name");\n        return false;\n    }\n''',
    '''    if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {\n        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n            minic_parser_error(parser, "expected block-scope extern function name");\n        }\n        return false;\n    }\n''',
    "converge block-scope extern direct names",
)

replace_once(
    "tests/compiler/c0/for_declaration_initializer.c",
    '''    for (int i = 0; i < 3; i++) {\n        sum += i;\n    }\n\n    int i = 7;\n''',
    '''    for (int i = 0; i < 3; i++) {\n        sum += i;\n    }\n\n    enum mod_mem_type { MOD_TEXT = 0, MOD_DATA = 1, MOD_MEM_NUM_TYPES = 2 };\n    for (enum mod_mem_type (type) = 0; type < MOD_MEM_NUM_TYPES; type++) {\n        sum += type;\n    }\n\n    int (parenthesized) = 3;\n    sum += parenthesized;\n\n    int i = 7;\n''',
    "Linux-shaped parenthesized local declarators",
)

replace_once(
    "tests/compiler/c0/for_declaration_initializer.c",
    '''    return sum == 10 && i == 7 ? 0 : 1;\n''',
    '''    return sum == 14 && i == 7 ? 0 : 1;\n''',
    "adjust parenthesized local runtime expectation",
)

replace_once(
    "tests/compiler/c0/run-for-declaration-initializers.sh",
    '''printf '%s\\n' 'PASS compiler/c0/for_declaration_initializer scope=condition,update,body redeclare-after-loop=1'\n''',
    '''printf '%s\\n' 'PASS compiler/c0/for_declaration_initializer scope=condition,update,body redeclare-after-loop=1 parenthesized-local=block+for shared-direct-declarator=1'\n''',
    "focused declarator pass contract",
)
