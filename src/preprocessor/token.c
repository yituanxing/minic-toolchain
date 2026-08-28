#include "minic/preprocessor.h"

const char *minic_pp_token_kind_name(MinicPpTokenKind kind) {
    switch (kind) {
    case MINIC_PP_TOKEN_IDENTIFIER:
        return "identifier";
    case MINIC_PP_TOKEN_NUMBER:
        return "number";
    case MINIC_PP_TOKEN_STRING:
        return "string";
    case MINIC_PP_TOKEN_CHARACTER:
        return "character";
    case MINIC_PP_TOKEN_PUNCTUATOR:
        return "punctuator";
    case MINIC_PP_TOKEN_NEWLINE:
        return "newline";
    case MINIC_PP_TOKEN_END:
        return "end";
    }
    return "invalid";
}
