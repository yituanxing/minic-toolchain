#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/token.h"
text = path.read_text()
text = replace_once(
    text,
    '''    MINIC_TOKEN_LESS,\n    MINIC_TOKEN_LESS_LESS,\n    MINIC_TOKEN_LESS_EQUAL,\n''',
    '''    MINIC_TOKEN_LESS,\n    MINIC_TOKEN_LESS_LESS,\n    MINIC_TOKEN_LESS_LESS_EQUAL,\n    MINIC_TOKEN_LESS_EQUAL,\n''',
    "token-shift-left-equal",
)
path.write_text(text)

path = root / "src/frontend/token.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case MINIC_TOKEN_LESS_LESS:\n        return "<<";\n    case MINIC_TOKEN_LESS_EQUAL:\n''',
    '''    case MINIC_TOKEN_LESS_LESS:\n        return "<<";\n    case MINIC_TOKEN_LESS_LESS_EQUAL:\n        return "<<=";\n    case MINIC_TOKEN_LESS_EQUAL:\n''',
    "token-name-shift-left-equal",
)
path.write_text(text)

path = root / "src/frontend/lexer.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case '<':\n        if (minic_lexer_peek_next(lexer) == '<') {\n            token->kind = MINIC_TOKEN_LESS_LESS;\n            minic_lexer_advance(lexer);\n        } else if (minic_lexer_peek_next(lexer) == '=') {\n''',
    '''    case '<':\n        if (minic_lexer_peek_next(lexer) == '<' && lexer->cursor + 2U < lexer->length &&\n            lexer->source[lexer->cursor + 2U] == '=') {\n            token->kind = MINIC_TOKEN_LESS_LESS_EQUAL;\n            minic_lexer_advance(lexer);\n            minic_lexer_advance(lexer);\n        } else if (minic_lexer_peek_next(lexer) == '<') {\n            token->kind = MINIC_TOKEN_LESS_LESS;\n            minic_lexer_advance(lexer);\n        } else if (minic_lexer_peek_next(lexer) == '=') {\n''',
    "lexer-shift-left-equal-longest-match",
)
path.write_text(text)

path = root / "src/frontend/parser_expression.c"
text = path.read_text()
text = replace_once(
    text,
    '''                                     parser->current.kind == MINIC_TOKEN_CARET_EQUAL ||\n                                     parser->current.kind == MINIC_TOKEN_GREATER_GREATER_EQUAL)) {\n''',
    '''                                     parser->current.kind == MINIC_TOKEN_CARET_EQUAL ||\n                                     parser->current.kind == MINIC_TOKEN_LESS_LESS_EQUAL ||\n                                     parser->current.kind == MINIC_TOKEN_GREATER_GREATER_EQUAL)) {\n''',
    "compound-shift-left-token",
)
text = replace_once(
    text,
    '''        case MINIC_TOKEN_CARET_EQUAL:\n            compound_operator = MINIC_BINARY_BITWISE_XOR;\n            break;\n        case MINIC_TOKEN_GREATER_GREATER_EQUAL:\n''',
    '''        case MINIC_TOKEN_CARET_EQUAL:\n            compound_operator = MINIC_BINARY_BITWISE_XOR;\n            break;\n        case MINIC_TOKEN_LESS_LESS_EQUAL:\n            compound_operator = MINIC_BINARY_SHIFT_LEFT;\n            break;\n        case MINIC_TOKEN_GREATER_GREATER_EQUAL:\n''',
    "compound-shift-left-operator",
)
path.write_text(text)

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(
    text,
    '''            operator_kind != MINIC_BINARY_BITWISE_OR && operator_kind != MINIC_BINARY_BITWISE_XOR &&\n            operator_kind != MINIC_BINARY_SHIFT_RIGHT) {\n''',
    '''            operator_kind != MINIC_BINARY_BITWISE_OR && operator_kind != MINIC_BINARY_BITWISE_XOR &&\n            operator_kind != MINIC_BINARY_SHIFT_LEFT && operator_kind != MINIC_BINARY_SHIFT_RIGHT) {\n''',
    "compound-shift-left-verifier",
)
path.write_text(text)

path = root / "src/target/riscv64/codegen_expression.c"
text = path.read_text()
text = replace_once(
    text,
    '''            case MINIC_BINARY_BITWISE_XOR:\n                opcode = "xor";\n                break;\n            case MINIC_BINARY_SHIFT_RIGHT:\n''',
    '''            case MINIC_BINARY_BITWISE_XOR:\n                opcode = "xor";\n                break;\n            case MINIC_BINARY_SHIFT_LEFT:\n                opcode = minic_type_is_long_integer(common_type) ? "sll" : "sllw";\n                break;\n            case MINIC_BINARY_SHIFT_RIGHT:\n''',
    "rv64-compound-shift-left",
)
path.write_text(text)

path = root / "tests/frontend/token_model_test.c"
text = path.read_text()
text = replace_once(
    text,
    '''        expect_name(MINIC_TOKEN_LESS_LESS, "<<") != 0 ||\n        expect_name(MINIC_TOKEN_GREATER_GREATER, ">>") != 0 ||\n''',
    '''        expect_name(MINIC_TOKEN_LESS_LESS, "<<") != 0 ||\n        expect_name(MINIC_TOKEN_LESS_LESS_EQUAL, "<<=") != 0 ||\n        expect_name(MINIC_TOKEN_GREATER_GREATER, ">>") != 0 ||\n        expect_name(MINIC_TOKEN_GREATER_GREATER_EQUAL, ">>=") != 0 ||\n''',
    "token-model-shift-assignment",
)
path.write_text(text)

path = root / "tests/frontend/lexer_test.c"
text = path.read_text()
old = '''    static const char source[] = "= == ! != < << <= > >> >=";\n    static const struct {\n        MinicTokenKind kind;\n        size_t column;\n    } expected[] = {\n        {MINIC_TOKEN_EQUAL, 1U},\n        {MINIC_TOKEN_EQUAL_EQUAL, 3U},\n        {MINIC_TOKEN_BANG, 6U},\n        {MINIC_TOKEN_BANG_EQUAL, 8U},\n        {MINIC_TOKEN_LESS, 11U},\n        {MINIC_TOKEN_LESS_LESS, 13U},\n        {MINIC_TOKEN_LESS_EQUAL, 16U},\n        {MINIC_TOKEN_GREATER, 19U},\n        {MINIC_TOKEN_GREATER_GREATER, 21U},\n        {MINIC_TOKEN_GREATER_EQUAL, 24U},\n        {MINIC_TOKEN_EOF, 26U}\n    };\n'''
new = '''    static const char source[] = "= == ! != < << <<= <= > >> >>= >=";\n    static const struct {\n        MinicTokenKind kind;\n        size_t column;\n    } expected[] = {\n        {MINIC_TOKEN_EQUAL, 1U},\n        {MINIC_TOKEN_EQUAL_EQUAL, 3U},\n        {MINIC_TOKEN_BANG, 6U},\n        {MINIC_TOKEN_BANG_EQUAL, 8U},\n        {MINIC_TOKEN_LESS, 11U},\n        {MINIC_TOKEN_LESS_LESS, 13U},\n        {MINIC_TOKEN_LESS_LESS_EQUAL, 16U},\n        {MINIC_TOKEN_LESS_EQUAL, 20U},\n        {MINIC_TOKEN_GREATER, 23U},\n        {MINIC_TOKEN_GREATER_GREATER, 25U},\n        {MINIC_TOKEN_GREATER_GREATER_EQUAL, 28U},\n        {MINIC_TOKEN_GREATER_EQUAL, 32U},\n        {MINIC_TOKEN_EOF, 34U}\n    };\n'''
text = replace_once(text, old, new, "lexer-shift-assignment-test")
path.write_text(text)

path = root / "tests/compiler/c0/compound_assignment_full.c"
text = path.read_text()
insert_after = '''static unsigned int update_bits(unsigned int value) {\n    value &= 0xffu;\n    value |= 0x100u;\n    value ^= 0x3u;\n    value >>= 2;\n    value *= 3u;\n    value -= 1u;\n    return value;\n}\n\n'''
addition = '''static unsigned int shift_left_unsigned(unsigned int value) {\n    value <<= 3;\n    return value;\n}\n\nstatic int shift_right_signed(int value) {\n    value >>= 2;\n    return value;\n}\n\n'''
text = replace_once(text, insert_after, insert_after + addition, "compound-shift-fixtures")
text = replace_once(
    text,
    '''                   update_bits(0x2ffu) == 380u && divide_unsigned(100ULL) == 10ULL &&\n''',
    '''                   update_bits(0x2ffu) == 380u && shift_left_unsigned(3u) == 24u &&\n                   shift_right_signed(-16) == -4 && divide_unsigned(100ULL) == 10ULL &&\n''',
    "compound-shift-main",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-compound-assignment-full.sh"
text = path.read_text()
text = replace_once(
    text,
    '''grep -F '  srlw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null\n''',
    '''grep -F '  sllw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null\ngrep -F '  srlw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null\ngrep -F '  sraw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null\n''',
    "compound-shift-opcode-gates",
)
text = replace_once(
    text,
    '''integer=+=,-=,*=,/=,%=,&=,|=,^=,>>= pointer=+,- double=+=,-=,*=,/= mixed-int-rhs=1 lvalue-evaluation=once''',
    '''integer=+=,-=,*=,/=,%=,&=,|=,^=,<<=,>>= shift-right=signed+unsigned pointer=+,- double=+=,-=,*=,/= mixed-int-rhs=1 lvalue-evaluation=once''',
    "compound-shift-summary",
)
path.write_text(text)
