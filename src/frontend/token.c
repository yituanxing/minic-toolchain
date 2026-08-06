#include "frontend/token.h"

const char *minic_token_kind_name(MinicTokenKind kind)
{
    switch (kind) {
    case MINIC_TOKEN_INVALID:
        return "invalid";
    case MINIC_TOKEN_EOF:
        return "end of file";
    case MINIC_TOKEN_IDENTIFIER:
        return "identifier";
    case MINIC_TOKEN_INTEGER_CONSTANT:
        return "integer constant";
    case MINIC_TOKEN_KW_CHAR:
        return "char";
    case MINIC_TOKEN_KW_INT:
        return "int";
    case MINIC_TOKEN_KW_UNSIGNED:
        return "unsigned";
    case MINIC_TOKEN_KW_VOID:
        return "void";
    case MINIC_TOKEN_KW_STRUCT:
        return "struct";
    case MINIC_TOKEN_KW_CONST:
        return "const";
    case MINIC_TOKEN_KW_TYPEDEF:
        return "typedef";
    case MINIC_TOKEN_KW_STATIC:
        return "static";
    case MINIC_TOKEN_KW_RETURN:
        return "return";
    case MINIC_TOKEN_KW_IF:
        return "if";
    case MINIC_TOKEN_KW_ELSE:
        return "else";
    case MINIC_TOKEN_KW_WHILE:
        return "while";
    case MINIC_TOKEN_KW_FOR:
        return "for";
    case MINIC_TOKEN_KW_BREAK:
        return "break";
    case MINIC_TOKEN_LPAREN:
        return "(";
    case MINIC_TOKEN_RPAREN:
        return ")";
    case MINIC_TOKEN_LBRACE:
        return "{";
    case MINIC_TOKEN_RBRACE:
        return "}";
    case MINIC_TOKEN_SEMICOLON:
        return ";";
    case MINIC_TOKEN_COMMA:
        return ",";
    case MINIC_TOKEN_PLUS:
        return "+";
    case MINIC_TOKEN_PLUS_PLUS:
        return "++";
    case MINIC_TOKEN_MINUS:
        return "-";
    case MINIC_TOKEN_MINUS_MINUS:
        return "--";
    case MINIC_TOKEN_ARROW:
        return "->";
    case MINIC_TOKEN_STAR:
        return "*";
    case MINIC_TOKEN_AMPERSAND:
        return "&";
    case MINIC_TOKEN_CARET:
        return "^";
    case MINIC_TOKEN_CARET_EQUAL:
        return "^=";
    case MINIC_TOKEN_SLASH:
        return "/";
    case MINIC_TOKEN_PERCENT:
        return "%";
    case MINIC_TOKEN_EQUAL:
        return "=";
    case MINIC_TOKEN_EQUAL_EQUAL:
        return "==";
    case MINIC_TOKEN_BANG:
        return "!";
    case MINIC_TOKEN_BANG_EQUAL:
        return "!=";
    case MINIC_TOKEN_LESS:
        return "<";
    case MINIC_TOKEN_LESS_LESS:
        return "<<";
    case MINIC_TOKEN_LESS_EQUAL:
        return "<=";
    case MINIC_TOKEN_GREATER:
        return ">";
    case MINIC_TOKEN_GREATER_GREATER:
        return ">>";
    case MINIC_TOKEN_GREATER_EQUAL:
        return ">=";
    case MINIC_TOKEN_LBRACKET:
        return "[";
    case MINIC_TOKEN_RBRACKET:
        return "]";
    case MINIC_TOKEN_KIND_COUNT:
        break;
    }
    return "unknown token";
}