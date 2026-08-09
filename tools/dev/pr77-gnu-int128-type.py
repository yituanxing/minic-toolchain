#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# ----- public/internal type model -----
path = Path("src/frontend/type.h")
text = path.read_text()
text = replace_once(
    text,
    """    MINIC_INTEGER_RANK_LONG,\n    MINIC_INTEGER_RANK_LONG_LONG\n""",
    """    MINIC_INTEGER_RANK_LONG,\n    MINIC_INTEGER_RANK_LONG_LONG,\n    MINIC_INTEGER_RANK_INT128\n""",
    "integer-rank",
)
text = replace_once(
    text,
    """MinicType minic_type_long_long(void);\nMinicType minic_type_unsigned_long_long(void);\n""",
    """MinicType minic_type_long_long(void);\nMinicType minic_type_unsigned_long_long(void);\nMinicType minic_type_int128(void);\nMinicType minic_type_unsigned_int128(void);\n""",
    "int128-constructors",
)
text = replace_once(
    text,
    """bool minic_type_is_long_integer(MinicType type);\n""",
    """bool minic_type_is_long_integer(MinicType type);\nbool minic_type_is_int128_integer(MinicType type);\n""",
    "int128-query-prototype",
)
path.write_text(text)


# ----- semantic type behavior -----
path = Path("src/frontend/type.c")
text = path.read_text()
text = replace_once(
    text,
    """MinicType minic_type_unsigned_long_long(void) {\n    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);\n}\n\nMinicType minic_type_float(void) {\n""",
    """MinicType minic_type_unsigned_long_long(void) {\n    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);\n}\n\nMinicType minic_type_int128(void) {\n    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT128);\n}\n\nMinicType minic_type_unsigned_int128(void) {\n    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT128);\n}\n\nMinicType minic_type_float(void) {\n""",
    "int128-constructor-impl",
)
text = replace_once(
    text,
    """    if (type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) {\n        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long_long()\n                                                       : minic_type_long_long();\n        return true;\n    }\n    return false;\n""",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) {\n        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long_long()\n                                                       : minic_type_long_long();\n        return true;\n    }\n    if (type.integer_rank == MINIC_INTEGER_RANK_INT128) {\n        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_int128()\n                                                       : minic_type_int128();\n        return true;\n    }\n    return false;\n""",
    "int128-promotion",
)
text = replace_once(
    text,
    """    /* On the active RV64 data model, signed long and signed long long both\n       represent every value of unsigned int. */\n    if (signed_type.integer_rank >= MINIC_INTEGER_RANK_LONG &&\n        unsigned_type.integer_rank <= MINIC_INTEGER_RANK_INT) {\n        *result = signed_type;\n        return true;\n    }\n\n    *result = minic_type_integer_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, signed_type.integer_rank);\n""",
    """    /* On the active RV64 data model, signed long and signed long long both\n       represent every value of unsigned int.  Signed __int128 additionally\n       represents every value of the narrower unsigned integer ranks. */\n    if ((signed_type.integer_rank >= MINIC_INTEGER_RANK_LONG &&\n         unsigned_type.integer_rank <= MINIC_INTEGER_RANK_INT) ||\n        (signed_type.integer_rank == MINIC_INTEGER_RANK_INT128 &&\n         unsigned_type.integer_rank <= MINIC_INTEGER_RANK_LONG_LONG)) {\n        *result = signed_type;\n        return true;\n    }\n\n    *result = minic_type_integer_with_rank(MINIC_INTEGER_SIGN_UNSIGNED, signed_type.integer_rank);\n""",
    "int128-common-type",
)
text = replace_once(
    text,
    """            type.integer_rank == MINIC_INTEGER_RANK_INT ||\n            type.integer_rank == MINIC_INTEGER_RANK_LONG ||\n            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) &&\n""",
    """            type.integer_rank == MINIC_INTEGER_RANK_INT ||\n            type.integer_rank == MINIC_INTEGER_RANK_LONG ||\n            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||\n            type.integer_rank == MINIC_INTEGER_RANK_INT128) &&\n""",
    "int128-valid-integer",
)
text = replace_once(
    text,
    """bool minic_type_is_long_integer(MinicType type) {\n    return minic_type_is_integer(type) && (type.integer_rank == MINIC_INTEGER_RANK_LONG ||\n                                           type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);\n}\n\nbool minic_type_is_signed_integer(MinicType type) {\n""",
    """bool minic_type_is_long_integer(MinicType type) {\n    return minic_type_is_integer(type) && (type.integer_rank == MINIC_INTEGER_RANK_LONG ||\n                                           type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);\n}\n\nbool minic_type_is_int128_integer(MinicType type) {\n    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_INT128;\n}\n\nbool minic_type_is_signed_integer(MinicType type) {\n""",
    "int128-query",
)
path.write_text(text)


# ----- GNU type spelling in the parser -----
path = Path("src/frontend/parser_type.c")
text = path.read_text()
include_anchor = '#include "frontend/parser_internal.h"\n\n'
helper = r'''#include "frontend/parser_internal.h"

#include <string.h>

static bool minic_parser_gnu_int128_name(const MinicParser *parser, bool *is_unsigned_name) {
    const char *name;
    size_t length;

    if (is_unsigned_name != NULL) {
        *is_unsigned_name = false;
    }
    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name = parser->source + parser->current.span.begin.offset;
    length = minic_parser_span_length(parser->current.span);
    if ((length == 8U && memcmp(name, "__int128", 8U) == 0) ||
        (length == 10U && memcmp(name, "__int128_t", 10U) == 0)) {
        return true;
    }
    if ((length == 9U && memcmp(name, "__uint128", 9U) == 0) ||
        (length == 11U && memcmp(name, "__uint128_t", 11U) == 0)) {
        if (is_unsigned_name != NULL) {
            *is_unsigned_name = true;
        }
        return true;
    }
    return false;
}

'''
text = replace_once(text, include_anchor, helper, "parser-int128-helper")

start_anchor = """    if (minic_parser_is_integer_type_specifier(parser->current.kind)) {\n        bool saw_char = false;\n"""
start_replacement = """    {\n        bool int128_unsigned_name = false;\n        bool starts_int128 = minic_parser_gnu_int128_name(parser, &int128_unsigned_name);\n\n        if (minic_parser_is_integer_type_specifier(parser->current.kind) || starts_int128) {\n        bool saw_char = false;\n"""
text = replace_once(text, start_anchor, start_replacement, "parser-int128-start")

selection_anchor = """        if (saw_signed && saw_unsigned) {\n            minic_parser_error(parser, \"conflicting signed and unsigned type specifiers\");\n            return false;\n        }\n        if (saw_short && long_count != 0U) {\n"""
selection_replacement = """        if (saw_signed && saw_unsigned) {\n            minic_parser_error(parser, \"conflicting signed and unsigned type specifiers\");\n            return false;\n        }\n        if (minic_parser_gnu_int128_name(parser, &int128_unsigned_name)) {\n            if (saw_char || saw_int || saw_short || long_count != 0U ||\n                (int128_unsigned_name && saw_signed)) {\n                minic_parser_error(parser, \"invalid specifiers for GNU __int128 type\");\n                return false;\n            }\n            parsed_type = (saw_unsigned || int128_unsigned_name) ? minic_type_unsigned_int128()\n                                                                 : minic_type_int128();\n            if (!minic_parser_advance(parser)) {\n                return false;\n            }\n        } else if (saw_short && long_count != 0U) {\n"""
text = replace_once(text, selection_anchor, selection_replacement, "parser-int128-select")

# The previous replacement turns the old following `if (saw_char)` chain into the
# `else if` continuation of the new int128 branch.
text = replace_once(
    text,
    """            return false;\n        }\n        if (saw_char) {\n""",
    """            return false;\n        } else if (saw_char) {\n""",
    "parser-int128-chain",
)

# Close the scope introduced around the integer/GNU-int128 decision immediately
# before the existing float branch.
text = replace_once(
    text,
    """        } else {\n            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();\n        }\n    } else if (parser->current.kind == MINIC_TOKEN_KW_FLOAT) {\n""",
    """        } else {\n            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();\n        }\n        } else if (parser->current.kind == MINIC_TOKEN_KW_FLOAT) {\n""",
    "parser-int128-float-chain",
)
# Match the extra opening block with a closing brace before qualifier processing.
text = replace_once(
    text,
    """    } else {\n        minic_parser_error(parser, \"expected type name\");\n        return false;\n    }\n\n    while (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n""",
    """        } else {\n            minic_parser_error(parser, \"expected type name\");\n            return false;\n        }\n    }\n\n    while (parser->current.kind == MINIC_TOKEN_KW_CONST) {\n""",
    "parser-int128-close-scope",
)
path.write_text(text)


# ----- RV64 data layout -----
path = Path("src/target/riscv64/layout.c")
text = path.read_text()
text = replace_once(
    text,
    """        } else if (minic_type_is_long_integer(type)) {\n            *size = 8U;\n            *alignment = 8U;\n        } else {\n""",
    """        } else if (minic_type_is_int128_integer(type)) {\n            *size = 16U;\n            *alignment = 16U;\n        } else if (minic_type_is_long_integer(type)) {\n            *size = 8U;\n            *alignment = 8U;\n        } else {\n""",
    "rv64-int128-layout",
)
path.write_text(text)

print("staged semantic GNU __int128/__uint128 types with RV64 size=16 align=16")
