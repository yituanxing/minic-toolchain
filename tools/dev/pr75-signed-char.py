#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/type.h",
    "MinicType minic_type_char(void);\nMinicType minic_type_unsigned_char(void);\n",
    "MinicType minic_type_char(void);\nMinicType minic_type_signed_char(void);\nMinicType minic_type_unsigned_char(void);\n",
)

replace_once(
    "src/frontend/type.c",
    """MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}
""",
    """MinicType minic_type_signed_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_CHAR);
}

MinicType minic_type_unsigned_char(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_CHAR);
}
""",
)

replace_once(
    "src/frontend/parser_type.c",
    """            if (saw_signed) {
                minic_parser_error(parser, "signed char is not supported");
                return false;
            }
            parsed_type = saw_unsigned ? minic_type_unsigned_char() : minic_type_char();
""",
    """            parsed_type = saw_signed   ? minic_type_signed_char()
                          : saw_unsigned ? minic_type_unsigned_char()
                                         : minic_type_char();
""",
)

print("staged signed char type identity")
