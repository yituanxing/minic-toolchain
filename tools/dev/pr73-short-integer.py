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
    """    MINIC_TOKEN_KW_INT,
    MINIC_TOKEN_KW_LONG,
    MINIC_TOKEN_KW_SIGNED,
""",
    """    MINIC_TOKEN_KW_INT,
    MINIC_TOKEN_KW_LONG,
    MINIC_TOKEN_KW_SHORT,
    MINIC_TOKEN_KW_SIGNED,
""",
)
replace_once(
    "src/frontend/lexer.c",
    """    if (length == 4U && memcmp(text, \"long\", 4U) == 0) {
        return MINIC_TOKEN_KW_LONG;
    }
    if (length == 6U && memcmp(text, \"signed\", 6U) == 0) {
""",
    """    if (length == 4U && memcmp(text, \"long\", 4U) == 0) {
        return MINIC_TOKEN_KW_LONG;
    }
    if (length == 5U && memcmp(text, \"short\", 5U) == 0) {
        return MINIC_TOKEN_KW_SHORT;
    }
    if (length == 6U && memcmp(text, \"signed\", 6U) == 0) {
""",
)
replace_once(
    "src/frontend/token.c",
    """    case MINIC_TOKEN_KW_LONG:
        return \"long\";
    case MINIC_TOKEN_KW_SIGNED:
""",
    """    case MINIC_TOKEN_KW_LONG:
        return \"long\";
    case MINIC_TOKEN_KW_SHORT:
        return \"short\";
    case MINIC_TOKEN_KW_SIGNED:
""",
)

replace_once(
    "src/frontend/type.h",
    """    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_CHAR,
    MINIC_INTEGER_RANK_INT,
""",
    """    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_CHAR,
    MINIC_INTEGER_RANK_SHORT,
    MINIC_INTEGER_RANK_INT,
""",
)
replace_once(
    "src/frontend/type.h",
    """MinicType minic_type_char(void);
MinicType minic_type_unsigned_char(void);
MinicType minic_type_int(void);
""",
    """MinicType minic_type_char(void);
MinicType minic_type_unsigned_char(void);
MinicType minic_type_short(void);
MinicType minic_type_unsigned_short(void);
MinicType minic_type_int(void);
""",
)
replace_once(
    "src/frontend/type.h",
    """bool minic_type_is_char_integer(MinicType type);
bool minic_type_is_plain_char(MinicType type);
bool minic_type_is_long_integer(MinicType type);
""",
    """bool minic_type_is_char_integer(MinicType type);
bool minic_type_is_plain_char(MinicType type);
bool minic_type_is_short_integer(MinicType type);
bool minic_type_is_long_integer(MinicType type);
""",
)
replace_once(
    "src/frontend/type.c",
    """MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_int(void) {
""",
    """MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_short(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_SHORT);
}

MinicType minic_type_unsigned_short(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_SHORT);
}

MinicType minic_type_int(void) {
""",
)
replace_once(
    "src/frontend/type.c",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_CHAR) {
        *result = minic_type_int();
        return true;
    }
""",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
        type.integer_rank == MINIC_INTEGER_RANK_SHORT) {
        *result = minic_type_int();
        return true;
    }
""",
)
replace_once(
    "src/frontend/type.c",
    """           (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG) &&
""",
    """           (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG) &&
""",
)
replace_once(
    "src/frontend/type.c",
    """bool minic_type_is_plain_char(MinicType type) {
    return minic_type_is_char_integer(type) && type.is_plain_char;
}

bool minic_type_is_long_integer(MinicType type) {
""",
    """bool minic_type_is_plain_char(MinicType type) {
    return minic_type_is_char_integer(type) && type.is_plain_char;
}

bool minic_type_is_short_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_SHORT;
}

bool minic_type_is_long_integer(MinicType type) {
""",
)

replace_once(
    "src/frontend/parser_type.c",
    """    return kind == MINIC_TOKEN_KW_CHAR || kind == MINIC_TOKEN_KW_INT ||
           kind == MINIC_TOKEN_KW_LONG || kind == MINIC_TOKEN_KW_SIGNED ||
           kind == MINIC_TOKEN_KW_UNSIGNED;
""",
    """    return kind == MINIC_TOKEN_KW_CHAR || kind == MINIC_TOKEN_KW_INT ||
           kind == MINIC_TOKEN_KW_LONG || kind == MINIC_TOKEN_KW_SHORT ||
           kind == MINIC_TOKEN_KW_SIGNED || kind == MINIC_TOKEN_KW_UNSIGNED;
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """        bool saw_int = false;
        bool saw_long = false;
        bool saw_signed = false;
""",
    """        bool saw_int = false;
        bool saw_long = false;
        bool saw_short = false;
        bool saw_signed = false;
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """            case MINIC_TOKEN_KW_LONG:
                if (saw_long) {
                    minic_parser_error(parser, \"long long is not supported\");
                    return false;
                }
                saw_long = true;
                break;
            case MINIC_TOKEN_KW_SIGNED:
""",
    """            case MINIC_TOKEN_KW_LONG:
                if (saw_long) {
                    minic_parser_error(parser, \"long long is not supported\");
                    return false;
                }
                saw_long = true;
                break;
            case MINIC_TOKEN_KW_SHORT:
                if (saw_short) {
                    minic_parser_error(parser, \"duplicate short type specifier\");
                    return false;
                }
                saw_short = true;
                break;
            case MINIC_TOKEN_KW_SIGNED:
""",
)
replace_once(
    "src/frontend/parser_type.c",
    """        if (saw_signed && saw_unsigned) {
            minic_parser_error(parser, \"conflicting signed and unsigned type specifiers\");
            return false;
        }
        if (saw_char) {
            if (saw_long || saw_int) {
                minic_parser_error(parser, \"char cannot be combined with int or long\");
                return false;
            }
            if (saw_signed) {
                minic_parser_error(parser, \"signed char is not supported\");
                return false;
            }
            parsed_type = saw_unsigned ? minic_type_unsigned_char() : minic_type_char();
        } else if (saw_long) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
        }
""",
    """        if (saw_signed && saw_unsigned) {
            minic_parser_error(parser, \"conflicting signed and unsigned type specifiers\");
            return false;
        }
        if (saw_short && saw_long) {
            minic_parser_error(parser, \"short cannot be combined with long\");
            return false;
        }
        if (saw_char) {
            if (saw_short || saw_long || saw_int) {
                minic_parser_error(parser, \"char cannot be combined with short, int, or long\");
                return false;
            }
            if (saw_signed) {
                minic_parser_error(parser, \"signed char is not supported\");
                return false;
            }
            parsed_type = saw_unsigned ? minic_type_unsigned_char() : minic_type_char();
        } else if (saw_short) {
            parsed_type = saw_unsigned ? minic_type_unsigned_short() : minic_type_short();
        } else if (saw_long) {
            parsed_type = saw_unsigned ? minic_type_unsigned_long() : minic_type_long();
        } else {
            parsed_type = saw_unsigned ? minic_type_unsigned_int() : minic_type_int();
        }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """               (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG);
""",
    """               (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG);
""",
)

replace_once(
    "src/target/riscv64/layout.c",
    """        if (minic_type_is_char_integer(type)) {
            *size = 1U;
            *alignment = 1U;
        } else if (minic_type_is_long_integer(type)) {
""",
    """        if (minic_type_is_char_integer(type)) {
            *size = 1U;
            *alignment = 1U;
        } else if (minic_type_is_short_integer(type)) {
            *size = 2U;
            *alignment = 2U;
        } else if (minic_type_is_long_integer(type)) {
""",
)

replace_once(
    "src/target/riscv64/codegen_support.c",
    """    *width = minic_type_is_char_integer(type) ? 1U : minic_type_is_long_integer(type) ? 8U : 4U;
""",
    """    *width = minic_type_is_char_integer(type)    ? 1U
             : minic_type_is_short_integer(type) ? 2U
             : minic_type_is_long_integer(type)  ? 8U
                                                 : 4U;
""",
)
replace_once(
    "src/target/riscv64/codegen_support.c",
    """    if (minic_type_is_long_integer(type)) {
        return \"ld\";
    }
    return minic_type_is_unsigned_integer(type) ? \"lwu\" : \"lw\";
""",
    """    if (minic_type_is_short_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? \"lhu\" : \"lh\";
    }
    if (minic_type_is_long_integer(type)) {
        return \"ld\";
    }
    return minic_type_is_unsigned_integer(type) ? \"lwu\" : \"lw\";
""",
)
replace_once(
    "src/target/riscv64/codegen_support.c",
    """    return minic_type_is_char_integer(type) ? \"sb\" : minic_type_is_long_integer(type) ? \"sd\" : \"sw\";
""",
    """    return minic_type_is_char_integer(type)    ? \"sb\"
           : minic_type_is_short_integer(type) ? \"sh\"
           : minic_type_is_long_integer(type)  ? \"sd\"
                                               : \"sw\";
""",
)
replace_once(
    "src/target/riscv64/codegen_support.c",
    """    if (minic_type_is_long_integer(type)) {
        return true;
    }
    if (minic_type_is_unsigned_integer(type)) {
""",
    """    if (minic_type_is_short_integer(type)) {
        return fprintf(file,
                       \"  slli %s, %s, 48\\n\"
                       \"  %s %s, %s, 48\\n\",
                       register_name,
                       register_name,
                       minic_type_is_unsigned_integer(type) ? \"srli\" : \"srai\",
                       register_name,
                       register_name) >= 0;
    }
    if (minic_type_is_long_integer(type)) {
        return true;
    }
    if (minic_type_is_unsigned_integer(type)) {
""",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """    *scalar_width = minic_type_is_char_integer(type)   ? 1U
                    : minic_type_is_long_integer(type) ? 8U
                                                       : 4U;
""",
    """    *scalar_width = minic_type_is_char_integer(type)    ? 1U
                    : minic_type_is_short_integer(type) ? 2U
                    : minic_type_is_long_integer(type)  ? 8U
                                                        : 4U;
""",
)
replace_once(
    "src/target/riscv64/codegen_function.c",
    """        directive = minic_type_is_char_integer(scalar_type)   ? \".byte\"
                    : minic_type_is_long_integer(scalar_type) ? \".dword\"
                                                              : \".word\";
""",
    """        directive = minic_type_is_char_integer(scalar_type)    ? \".byte\"
                    : minic_type_is_short_integer(scalar_type) ? \".half\"
                    : minic_type_is_long_integer(scalar_type)  ? \".dword\"
                                                               : \".word\";
""",
)

print("staged signed and unsigned short integer semantics")
