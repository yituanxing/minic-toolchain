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
    "src/frontend/token.h",
    "    MINIC_TOKEN_KW_CONST,\n    MINIC_TOKEN_KW_TYPEDEF,\n",
    "    MINIC_TOKEN_KW_CONST,\n    MINIC_TOKEN_KW_VOLATILE,\n    MINIC_TOKEN_KW_TYPEDEF,\n",
)

replace_once(
    "src/frontend/lexer.c",
    '''    if (length == 5U && memcmp(text, "const", 5U) == 0) {\n        return MINIC_TOKEN_KW_CONST;\n    }\n''',
    '''    if (length == 5U && memcmp(text, "const", 5U) == 0) {\n        return MINIC_TOKEN_KW_CONST;\n    }\n    if (length == 8U && memcmp(text, "volatile", 8U) == 0) {\n        return MINIC_TOKEN_KW_VOLATILE;\n    }\n''',
)

replace_once(
    "src/frontend/token.c",
    '''    case MINIC_TOKEN_KW_CONST:\n        return "const";\n''',
    '''    case MINIC_TOKEN_KW_CONST:\n        return "const";\n    case MINIC_TOKEN_KW_VOLATILE:\n        return "volatile";\n''',
)

replace_once(
    "src/frontend/type.h",
    '''typedef enum MinicTypeQualifier {\n    MINIC_TYPE_QUALIFIER_NONE = 0,\n    MINIC_TYPE_QUALIFIER_CONST = 1U << 0\n} MinicTypeQualifier;\n''',
    '''typedef enum MinicTypeQualifier {\n    MINIC_TYPE_QUALIFIER_NONE = 0,\n    MINIC_TYPE_QUALIFIER_CONST = 1U << 0,\n    MINIC_TYPE_QUALIFIER_VOLATILE = 1U << 1\n} MinicTypeQualifier;\n''',
)

replace_once(
    "src/frontend/type.h",
    "bool minic_type_add_const(MinicType type, MinicType *result);\n",
    "bool minic_type_add_const(MinicType type, MinicType *result);\nbool minic_type_add_volatile(MinicType type, MinicType *result);\n",
)
replace_once(
    "src/frontend/type.h",
    "bool minic_type_is_const(MinicType type);\n",
    "bool minic_type_is_const(MinicType type);\nbool minic_type_is_volatile(MinicType type);\n",
)

replace_once(
    "src/frontend/type.c",
    '''bool minic_type_unqualified(MinicType type, MinicType *result) {\n''',
    '''bool minic_type_add_volatile(MinicType type, MinicType *result) {\n    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type) ||\n        minic_type_is_function(type) || type.pointer_depth != 0U) {\n        return false;\n    }\n    *result = type;\n    result->base_qualifiers |= MINIC_TYPE_QUALIFIER_VOLATILE;\n    return true;\n}\n\nbool minic_type_unqualified(MinicType type, MinicType *result) {\n''',
)

replace_once(
    "src/frontend/type.c",
    '''bool minic_type_is_void(MinicType type) {\n''',
    '''bool minic_type_is_volatile(MinicType type) {\n    return minic_type_pointer_qualifiers_are_valid(type) && type.pointer_depth == 0U &&\n           (type.base_qualifiers & MINIC_TYPE_QUALIFIER_VOLATILE) != 0U;\n}\n\nbool minic_type_is_void(MinicType type) {\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        (type.base_qualifiers & ~((unsigned int)MINIC_TYPE_QUALIFIER_CONST)) != 0U ||\n''',
    '''        (type.base_qualifiers &\n         ~((unsigned int)MINIC_TYPE_QUALIFIER_CONST |\n           (unsigned int)MINIC_TYPE_QUALIFIER_VOLATILE)) != 0U ||\n''',
)

path = Path("src/frontend/parser_type.c")
text = path.read_text()
start = text.find("bool minic_parser_parse_type_specifiers(MinicParser *parser, MinicType *type) {")
end = text.find("\nbool minic_parser_parse_pointer_declarator", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate parse_type_specifiers")
old_fn = text[start:end]
old_fn = old_fn.replace("    bool is_const;\n", "    bool is_const;\n    bool is_volatile;\n", 1)
old_fn = old_fn.replace(
    '''    is_const = false;\n    if (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n        is_const = true;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n''',
    '''    is_const = false;\n    is_volatile = false;\n    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||\n           parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n            is_const = true;\n        } else {\n            is_volatile = true;\n        }\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n''',
    1,
)
old_fn = old_fn.replace(
    '''    while (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n        is_const = true;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {\n        minic_parser_error(parser, "cannot apply const qualifier");\n        return false;\n    }\n''',
    '''    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||\n           parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n            is_const = true;\n        } else {\n            is_volatile = true;\n        }\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n    if (is_const && !minic_type_add_const(parsed_type, &parsed_type)) {\n        minic_parser_error(parser, "cannot apply const qualifier");\n        return false;\n    }\n    if (is_volatile && !minic_type_add_volatile(parsed_type, &parsed_type)) {\n        minic_parser_error(parser, "cannot apply volatile qualifier");\n        return false;\n    }\n''',
    1,
)
if old_fn == text[start:end]:
    raise SystemExit("parse_type_specifiers qualifier rewrite made no change")
text = text[:start] + old_fn + text[end:]
path.write_text(text)

replace_once(
    "src/frontend/parser_type.c",
    '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n            if (!minic_type_add_const(parsed_type, &parsed_type)) {\n                minic_parser_error(parser, "cannot apply pointer const qualifier");\n                return false;\n            }\n            if (!minic_parser_advance(parser)) {\n                return false;\n            }\n        }\n''',
    '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||\n               parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n            if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {\n                minic_parser_error(parser, "pointer volatile qualifier is not supported yet");\n                return false;\n            }\n            if (!minic_type_add_const(parsed_type, &parsed_type)) {\n                minic_parser_error(parser, "cannot apply pointer const qualifier");\n                return false;\n            }\n            if (!minic_parser_advance(parser)) {\n                return false;\n            }\n        }\n''',
)

replace_once(
    "src/frontend/parser_statement.c",
    '''    case MINIC_TOKEN_KW_CONST:\n    case MINIC_TOKEN_KW_CHAR:\n''',
    '''    case MINIC_TOKEN_KW_CONST:\n    case MINIC_TOKEN_KW_VOLATILE:\n    case MINIC_TOKEN_KW_CHAR:\n''',
)

print("staged base volatile type qualifiers")
