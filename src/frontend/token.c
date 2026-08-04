#include "frontend/token.h"

const char *minic_token_kind_name(MinicTokenKind kind)
{
    static const char *const names[MINIC_TOKEN_KIND_COUNT] = {
        "invalid",
        "end of file",
        "identifier",
        "integer constant",
        "int",
        "void",
        "return",
        "(",
        ")",
        "{",
        "}",
        ";"
    };

    if (kind < MINIC_TOKEN_INVALID || kind >= MINIC_TOKEN_KIND_COUNT) {
        return "unknown token";
    }
    return names[kind];
}
