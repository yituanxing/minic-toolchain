#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_in_function(path: str, signature: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"{label}: missing {signature}")
    candidates = [
        pos
        for pos in (
            text.find("\nstatic ", start + len(signature)),
            text.find("\nbool ", start + len(signature)),
            text.find("\nvoid ", start + len(signature)),
            text.find("\nMinicType ", start + len(signature)),
        )
        if pos >= 0
    ]
    end = min(candidates) if candidates else len(text)
    body = text[start:end]
    count = body.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor in function, found {count}")
    target.write_text(text[:start] + body.replace(old, new, 1) + text[end:])


replace_once(
    "src/frontend/token.h",
    """    MINIC_TOKEN_STRING_LITERAL,
    MINIC_TOKEN_KW_CHAR,
""",
    """    MINIC_TOKEN_STRING_LITERAL,
    MINIC_TOKEN_KW_BOOL,
    MINIC_TOKEN_KW_CHAR,
""",
    "bool-token-enum",
)
replace_once(
    "src/frontend/token.c",
    """    case MINIC_TOKEN_STRING_LITERAL:
        return "string literal";
    case MINIC_TOKEN_KW_CHAR:
""",
    """    case MINIC_TOKEN_STRING_LITERAL:
        return "string literal";
    case MINIC_TOKEN_KW_BOOL:
        return "_Bool";
    case MINIC_TOKEN_KW_CHAR:
""",
    "bool-token-name",
)
replace_once(
    "src/frontend/lexer.c",
    """    if (length == 4U && memcmp(text, "char", 4U) == 0) {
        return MINIC_TOKEN_KW_CHAR;
    }
""",
    """    if (length == 5U && memcmp(text, "_Bool", 5U) == 0) {
        return MINIC_TOKEN_KW_BOOL;
    }
    if (length == 4U && memcmp(text, "char", 4U) == 0) {
        return MINIC_TOKEN_KW_CHAR;
    }
""",
    "bool-lexer-keyword",
)

replace_once(
    "src/frontend/type.h",
    """typedef enum MinicIntegerRank {
    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_CHAR,
""",
    """typedef enum MinicIntegerRank {
    MINIC_INTEGER_RANK_NONE = 0,
    MINIC_INTEGER_RANK_BOOL,
    MINIC_INTEGER_RANK_CHAR,
""",
    "bool-rank",
)
replace_once(
    "src/frontend/type.h",
    """MinicType minic_type_void(void);
MinicType minic_type_char(void);
""",
    """MinicType minic_type_void(void);
MinicType minic_type_bool(void);
MinicType minic_type_char(void);
""",
    "bool-constructor-prototype",
)
replace_once(
    "src/frontend/type.h",
    """bool minic_type_is_integer(MinicType type);
bool minic_type_is_char_integer(MinicType type);
""",
    """bool minic_type_is_integer(MinicType type);
bool minic_type_is_bool_integer(MinicType type);
bool minic_type_is_char_integer(MinicType type);
""",
    "bool-query-prototype",
)
replace_once(
    "src/frontend/type.c",
    """MinicType minic_type_void(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_VOID);
}

MinicType minic_type_char(void) {
""",
    """MinicType minic_type_void(void) {
    return minic_type_scalar(MINIC_TYPE_BASE_VOID);
}

MinicType minic_type_bool(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_BOOL);
}

MinicType minic_type_char(void) {
""",
    "bool-constructor",
)
replace_in_function(
    "src/frontend/type.c",
    "bool minic_type_integer_promotion(",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
        type.integer_rank == MINIC_INTEGER_RANK_SHORT) {
""",
    """    if (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
        type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
        type.integer_rank == MINIC_INTEGER_RANK_SHORT) {
""",
    "bool-promotion",
)
replace_once(
    "src/frontend/type.c",
    """           (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
""",
    """           (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
            type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
""",
    "bool-valid-integer",
)
replace_once(
    "src/frontend/type.c",
    """bool minic_type_is_char_integer(MinicType type) {
""",
    """bool minic_type_is_bool_integer(MinicType type) {
    return minic_type_is_integer(type) && type.integer_rank == MINIC_INTEGER_RANK_BOOL;
}

bool minic_type_is_char_integer(MinicType type) {
""",
    "bool-query",
)

replace_once(
    "src/frontend/parser_type.c",
    """    if (minic_parser_is_integer_type_specifier(parser->current.kind)) {
        bool saw_char = false;
""",
    """    if (parser->current.kind == MINIC_TOKEN_KW_BOOL) {
        parsed_type = minic_type_bool();
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (minic_parser_is_integer_type_specifier(parser->current.kind)) {
        bool saw_char = false;
""",
    "bool-type-parser",
)
replace_once(
    "src/frontend/parser_statement.c",
    """    case MINIC_TOKEN_KW_CHAR:
""",
    """    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
""",
    "bool-local-declaration",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """                type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
""",
    """                type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
                type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
""",
    "bool-verifier",
)
replace_once(
    "src/frontend/parser_core.c",
    """        switch (type.integer_rank) {
        case MINIC_INTEGER_RANK_CHAR:
""",
    """        switch (type.integer_rank) {
        case MINIC_INTEGER_RANK_BOOL:
        case MINIC_INTEGER_RANK_CHAR:
""",
    "bool-constant-layout",
)
replace_in_function(
    "src/target/riscv64/layout.c",
    "bool minic_riscv64_type_layout(",
    """        if (minic_type_is_char_integer(type)) {
            *size = 1U;
            *alignment = 1U;
""",
    """        if (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) {
            *size = 1U;
            *alignment = 1U;
""",
    "bool-target-layout",
)

replace_in_function(
    "src/target/riscv64/codegen_support.c",
    "static bool minic_riscv64_scalar_width(",
    """    *width = minic_type_is_char_integer(type)    ? 1U
""",
    """    *width = (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? 1U
""",
    "bool-scalar-width",
)
replace_in_function(
    "src/target/riscv64/codegen_support.c",
    "static const char *minic_riscv64_load_instruction(",
    """    if (minic_type_is_char_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lbu" : "lb";
    }
""",
    """    if (minic_type_is_bool_integer(type)) {
        return "lbu";
    }
    if (minic_type_is_char_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lbu" : "lb";
    }
""",
    "bool-load",
)
replace_in_function(
    "src/target/riscv64/codegen_support.c",
    "static const char *minic_riscv64_store_instruction(",
    """    return minic_type_is_char_integer(type)    ? "sb"
""",
    """    return (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? "sb"
""",
    "bool-store",
)
replace_in_function(
    "src/target/riscv64/codegen_support.c",
    "bool minic_riscv64_emit_integer_conversion(",
    """    if (minic_type_is_char_integer(type)) {
""",
    """    if (minic_type_is_bool_integer(type)) {
        return fprintf(file, "  snez %s, %s\\n", register_name, register_name) >= 0;
    }
    if (minic_type_is_char_integer(type)) {
""",
    "bool-conversion",
)
replace_in_function(
    "src/target/riscv64/codegen_statement.c",
    "static bool minic_riscv64_emit_assignment(",
    """           fprintf(file, "  ld t0, 0(sp)\\n  addi sp, sp, 16\\n") >= 0 &&
           minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0");
""",
    """           fprintf(file, "  ld t0, 0(sp)\\n  addi sp, sp, 16\\n") >= 0 &&
           (!minic_type_is_integer(target->type) ||
            minic_riscv64_emit_integer_conversion(file, target->type, "t0")) &&
           minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0");
""",
    "bool-assignment-normalization",
)

print("staged C _Bool as a distinct rank with RV64 byte layout and nonzero-to-one conversion")
