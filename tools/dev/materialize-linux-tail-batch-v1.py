#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


# 1. The shared inferred-character-array owner already handles both string literals
# and braced scalar/designated lists. Mutable char arrays were rejected only because
# this helper accidentally required a const-qualified element type and then hard-coded
# the backing object as read-only.
replace_once(
    "src/frontend/parser_global.c",
    """        !minic_type_is_char_integer(element_type) || !minic_type_is_const(element_type) ||\n        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||\n""",
    """        !minic_type_is_char_integer(element_type) ||\n        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||\n""",
    "mutable inferred character array guard",
)
replace_once(
    "src/frontend/parser_global.c",
    """                                            object_type,\n                                            true,\n                                            true,\n                                            &object_id) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, \"expected '=' after static array\")) {\n""",
    """                                            object_type,\n                                            true,\n                                            minic_type_is_const(element_type),\n                                            &object_id) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, \"expected '=' after static array\")) {\n""",
    "mutable inferred character array storage mutability",
)

# 2. RV64 call lowering already owns register/stack placement beyond a7. The 16-value
# ceiling is only AST representation capacity, so raise that representation bound without
# changing ABI rules.
replace_once(
    "src/frontend/ast.h",
    "#define MINIC_MAX_FUNCTION_PARAMETERS 16U\n",
    "#define MINIC_MAX_FUNCTION_PARAMETERS 32U\n",
    "function/call representation capacity",
)

# Permanent focused coverage for mutable inferred character arrays.
replace_once(
    "tests/compiler/c0/static_storage_scalar_array_owner.c",
    """static int *external_objects[] = {\n    [STATIC_ARRAY_ONE] = &global_target,\n    [STATIC_ARRAY_THREE] = &global_target,\n};\n\n""",
    """static int *external_objects[] = {\n    [STATIC_ARRAY_ONE] = &global_target,\n    [STATIC_ARRAY_THREE] = &global_target,\n};\nstatic char mutable_codes[] = {\n    [STATIC_ARRAY_ZERO] = 'a',\n    [STATIC_ARRAY_THREE] = 'z',\n};\nstatic char mutable_string[] = \"abc\";\nstatic unsigned char mutable_bytes[] = \"*?\";\n\n""",
    "mutable inferred character array coverage declarations",
)
replace_once(
    "tests/compiler/c0/static_storage_scalar_array_owner.c",
    """                   matrix[0][1] == 2 && matrix[1][0] == 3\n               ? 0\n""",
    """                   matrix[0][1] == 2 && matrix[1][0] == 3 && mutable_codes[0] == 'a' &&\n                   mutable_codes[3] == 'z' && mutable_string[2] == 'c' && mutable_bytes[0] == '*' &&\n                   mutable_bytes[1] == '?'\n               ? 0\n""",
    "mutable inferred character array coverage checks",
)

# Turn the existing variadic call runtime differential into a >16 argument proof.
Path("tests/compiler/c0/variadic_direct_call.c").write_text(
    """int verify_variadic(int tag, ...);\n\nint main(void)\n{\n    char small;\n    long wide;\n    int value;\n    double precise;\n\n    small = 7;\n    wide = 1234;\n    value = 29;\n    precise = 2.5;\n    return verify_variadic(5, 11, small, wide, &value, precise,\n                           61, 62, 63, 64, 65, 66, 67, 68, 69,\n                           70, 71, 72, 73, 74);\n}\n"""
)
Path("tests/compiler/c0/variadic_direct_call_helper.c").write_text(
    """#include <stdarg.h>\n\nint verify_variadic(int tag, ...)\n{\n    va_list arguments;\n    int first;\n    int promoted_char;\n    long wide;\n    int *pointer;\n    double precise;\n    int expected;\n\n    va_start(arguments, tag);\n    first = va_arg(arguments, int);\n    promoted_char = va_arg(arguments, int);\n    wide = va_arg(arguments, long);\n    pointer = va_arg(arguments, int *);\n    precise = va_arg(arguments, double);\n\n    if (tag != 5 || first != 11 || promoted_char != 7 || wide != 1234 ||\n        pointer == 0 || *pointer != 29 || precise != 2.5) {\n        va_end(arguments);\n        return 1;\n    }\n    for (expected = 61; expected <= 74; ++expected) {\n        if (va_arg(arguments, int) != expected) {\n            va_end(arguments);\n            return 2;\n        }\n    }\n    va_end(arguments);\n    return 0;\n}\n"""
)
Path("tests/compiler/c0/invalid_variadic_too_many_arguments.c").write_text(
    """int consume(int tag, ...);\n\nint main(void)\n{\n    return consume(1,\n                   2, 3, 4, 5, 6, 7, 8, 9, 10, 11,\n                   12, 13, 14, 15, 16, 17, 18, 19, 20, 21,\n                   22, 23, 24, 25, 26, 27, 28, 29, 30, 31,\n                   32, 33, 34);\n}\n"""
)
replace_once(
    "tests/compiler/c0/run-variadic-direct-calls.sh",
    "abi=rv64-varargs fixed=1 extras=int,char,long,pointer,double,int,int,int stack=yes",
    "abi=rv64-varargs fixed=1 total-arguments=20 stack=yes capacity=32",
    "variadic direct call status text",
)

print("staged suffix-section + mutable inferred char arrays + 32-call-argument capacity")
