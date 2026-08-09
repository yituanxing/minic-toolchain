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
    "src/frontend/type.h",
    """    MINIC_INTEGER_RANK_INT,
    MINIC_INTEGER_RANK_LONG
} MinicIntegerRank;
""",
    """    MINIC_INTEGER_RANK_INT,
    MINIC_INTEGER_RANK_LONG,
    MINIC_INTEGER_RANK_LONG_LONG
} MinicIntegerRank;
""",
)
replace_once(
    "src/frontend/type.h",
    """MinicType minic_type_long(void);
MinicType minic_type_unsigned_long(void);
MinicType minic_type_float(void);
""",
    """MinicType minic_type_long(void);
MinicType minic_type_unsigned_long(void);
MinicType minic_type_long_long(void);
MinicType minic_type_unsigned_long_long(void);
MinicType minic_type_float(void);
""",
)

replace_once(
    "src/frontend/type.c",
    """MinicType minic_type_unsigned_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG);
}

MinicType minic_type_float(void) {
""",
    """MinicType minic_type_unsigned_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG);
}

MinicType minic_type_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_unsigned_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_float(void) {
""",
)
replace_once(
    "src/frontend/type.c",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_LONG) {
        *result =
            minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long() : minic_type_long();
        return true;
    }
    return false;
""",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_LONG) {
        *result =
            minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long() : minic_type_long();
        return true;
    }
    if (type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) {
        *result = minic_type_is_unsigned_integer(type) ? minic_type_unsigned_long_long()
                                                       : minic_type_long_long();
        return true;
    }
    return false;
""",
)
replace_once(
    "src/frontend/type.c",
    """    /* On the active RV64 data model, signed long represents every unsigned int value. */
    if (signed_type.integer_rank == MINIC_INTEGER_RANK_LONG &&
        unsigned_type.integer_rank == MINIC_INTEGER_RANK_INT) {
        *result = signed_type;
        return true;
    }
""",
    """    /* On the active RV64 data model, signed long and signed long long both
       represent every value of unsigned int. */
    if (signed_type.integer_rank >= MINIC_INTEGER_RANK_LONG &&
        unsigned_type.integer_rank <= MINIC_INTEGER_RANK_INT) {
        *result = signed_type;
        return true;
    }
""",
)
replace_once(
    "src/frontend/type.c",
    """            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG) &&
""",
    """            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) &&
""",
)
replace_once(
    "src/frontend/type.c",
    """bool minic_type_is_long_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_LONG;
}
""",
    """bool minic_type_is_long_integer(MinicType type) {
    return minic_type_is_integer(type) &&
           (type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
}
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG);
""",
    """                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
""",
)

replace_once(
    "src/frontend/parser_type.c",
    """        bool saw_int = false;
        bool saw_long = false;
        bool saw_short = false;
""",
    """        bool saw_int = false;
        unsigned int long_count = 0U;
        bool saw_short = false;
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """            case MINIC_TOKEN_KW_LONG:
                if (saw_long) {
                    minic_parser_error(parser, "long long is not supported");
                    return false;
                }
                saw_long = true;
                break;
""",
    """            case MINIC_TOKEN_KW_LONG:
                long_count += 1U;
                if (long_count > 2U) {
                    minic_parser_error(parser, "too many long type specifiers");
                    return false;
                }
                break;
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """        if (saw_short && saw_long) {
            minic_parser_error(parser, "short cannot be combined with long");
            return false;
        }
        if (saw_char) {
            if (saw_short || saw_long || saw_int) {
""",
    """        if (saw_short && long_count != 0U) {
            minic_parser_error(parser, "short cannot be combined with long");
            return false;
        }
        if (saw_char) {
            if (saw_short || long_count != 0U || saw_int) {
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """        } else if (saw_long) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
""",
    """        } else if (long_count == 2U) {
            parsed_type =
                saw_unsigned ? minic_type_unsigned_long_long() : minic_type_long_long();
        } else if (long_count == 1U) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
""",
)

# Integer-token suffix scanner: permit LL/ULL while retaining one unsigned suffix.
replace_once(
    "src/frontend/lexer.c",
    """    bool saw_long;
    bool saw_unsigned;
    size_t suffix_count;

    saw_long = false;
    saw_unsigned = false;
""",
    """    unsigned int long_count;
    bool saw_unsigned;
    size_t suffix_count;

    long_count = 0U;
    saw_unsigned = false;
""",
)
replace_once(
    "src/frontend/lexer.c",
    """        if (suffix == 'l' || suffix == 'L') {
            if (saw_long) {
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "long long constants are not supported");
                return false;
            }
            saw_long = true;
        } else {
""",
    """        if (suffix == 'l' || suffix == 'L') {
            long_count += 1U;
            if (long_count > 2U) {
                minic_lexer_set_message(
                    lexer, diagnostic, begin, "too many long integer suffixes");
                return false;
            }
        } else {
""",
)
replace_once(
    "src/frontend/lexer.c",
    """        if (suffix_count > 2U) {
            minic_lexer_set_message(
                lexer, diagnostic, begin, "unsupported integer constant suffix");
            return false;
        }
""",
    """        if (suffix_count > 3U) {
            minic_lexer_set_message(
                lexer, diagnostic, begin, "unsupported integer constant suffix");
            return false;
        }
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    bool saw_long;
    bool saw_unsigned;
    size_t offset;

    saw_long = false;
""",
    """    unsigned int long_count;
    bool saw_unsigned;
    size_t offset;

    long_count = 0U;
""",
)
replace_once(
    "src/frontend/parser_expression.c",
    """        if (character == 'l' || character == 'L') {
            saw_long = true;
        } else if (character == 'u' || character == 'U') {
""",
    """        if (character == 'l' || character == 'L') {
            long_count += 1U;
        } else if (character == 'u' || character == 'U') {
""",
)
replace_once(
    "src/frontend/parser_expression.c",
    """    if (saw_long) {
        return saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
    }
""",
    """    if (long_count >= 2U) {
        return saw_unsigned ? minic_type_unsigned_long_long() : minic_type_long_long();
    }
    if (long_count == 1U) {
        return saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
    }
""",
)

print("staged signed/unsigned long long rank and LL/ULL suffix typing")
