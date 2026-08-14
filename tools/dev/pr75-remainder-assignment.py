#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/token.h",
    "    MINIC_TOKEN_PERCENT,\n",
    "    MINIC_TOKEN_PERCENT,\n    MINIC_TOKEN_PERCENT_EQUAL,\n",
    "percent-equal token kind",
)

replace_once(
    "src/frontend/token.c",
    '''    case MINIC_TOKEN_PERCENT:\n        return "%";\n''',
    '''    case MINIC_TOKEN_PERCENT:\n        return "%";\n    case MINIC_TOKEN_PERCENT_EQUAL:\n        return "%=";\n''',
    "percent-equal token name",
)

replace_once(
    "src/frontend/lexer.c",
    '''    case '%':\n        token->kind = MINIC_TOKEN_PERCENT;\n        break;\n''',
    '''    case '%':\n        if (minic_lexer_peek_next(lexer) == '=') {\n            token->kind = MINIC_TOKEN_PERCENT_EQUAL;\n            minic_lexer_advance(lexer);\n        } else {\n            token->kind = MINIC_TOKEN_PERCENT;\n        }\n        break;\n''',
    "percent-equal lexer",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL ||\n         parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL ||\n''',
    '''         parser->current.kind == MINIC_TOKEN_SLASH_EQUAL ||\n         parser->current.kind == MINIC_TOKEN_PERCENT_EQUAL ||\n         parser->current.kind == MINIC_TOKEN_AMPERSAND_EQUAL ||\n''',
    "percent-equal parser dispatch",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''        case MINIC_TOKEN_SLASH_EQUAL:\n            compound_operator = MINIC_BINARY_DIVIDE;\n            break;\n        case MINIC_TOKEN_AMPERSAND_EQUAL:\n''',
    '''        case MINIC_TOKEN_SLASH_EQUAL:\n            compound_operator = MINIC_BINARY_DIVIDE;\n            break;\n        case MINIC_TOKEN_PERCENT_EQUAL:\n            compound_operator = MINIC_BINARY_REMAINDER;\n            break;\n        case MINIC_TOKEN_AMPERSAND_EQUAL:\n''',
    "percent-equal parser operator",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&\n            operator_kind != MINIC_BINARY_BITWISE_AND && operator_kind != MINIC_BINARY_BITWISE_OR &&\n''',
    '''            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&\n            operator_kind != MINIC_BINARY_REMAINDER &&\n            operator_kind != MINIC_BINARY_BITWISE_AND && operator_kind != MINIC_BINARY_BITWISE_OR &&\n''',
    "percent-equal verifier",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''            case MINIC_BINARY_DIVIDE:\n                if (minic_type_is_unsigned_integer(common_type)) {\n                    opcode = minic_type_is_long_integer(common_type) ? "divu" : "divuw";\n                } else {\n                    opcode = minic_type_is_long_integer(common_type) ? "div" : "divw";\n                }\n                break;\n            case MINIC_BINARY_BITWISE_AND:\n''',
    '''            case MINIC_BINARY_DIVIDE:\n                if (minic_type_is_unsigned_integer(common_type)) {\n                    opcode = minic_type_is_long_integer(common_type) ? "divu" : "divuw";\n                } else {\n                    opcode = minic_type_is_long_integer(common_type) ? "div" : "divw";\n                }\n                break;\n            case MINIC_BINARY_REMAINDER:\n                if (minic_type_is_unsigned_integer(common_type)) {\n                    opcode = minic_type_is_long_integer(common_type) ? "remu" : "remuw";\n                } else {\n                    opcode = minic_type_is_long_integer(common_type) ? "rem" : "remw";\n                }\n                break;\n            case MINIC_BINARY_BITWISE_AND:\n''',
    "percent-equal RV64 codegen",
)

print("staged %= compound assignment with RV64 rem/remu")
