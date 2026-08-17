#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(relative_path: str, old: str, new: str) -> None:
    path = root / relative_path
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative_path}: anchor count={count}")
    path.write_text(text.replace(old, new, 1))


# Synthetic members created while zero-initializing a const aggregate must carry
# the same propagated const qualification as direct initializer members.
replace_once(
    "src/frontend/parser_statement.c",
    """        member.kind = MINIC_EXPRESSION_MEMBER;\n        member.span = initializer_span;\n        member.type = field->type;\n        member.value_category = MINIC_VALUE_LVALUE;\n""",
    """        member.kind = MINIC_EXPRESSION_MEMBER;\n        member.span = initializer_span;\n        member.type = field->type;\n        if (minic_type_is_const(base_type) && !minic_type_add_const(member.type, &member.type)) {\n            minic_parser_error(parser, \"cannot propagate const to zero-initialized record member\");\n            return false;\n        }\n        member.value_category = MINIC_VALUE_LVALUE;\n""",
)

# Function-body ownership treats record initialization like the other leaf statements.
replace_once(
    "src/frontend/function_body.c",
    """    case MINIC_STATEMENT_ASSIGN:\n    case MINIC_STATEMENT_RECORD_COPY:\n    case MINIC_STATEMENT_XOR_ASSIGN:\n""",
    """    case MINIC_STATEMENT_ASSIGN:\n    case MINIC_STATEMENT_RECORD_COPY:\n    case MINIC_STATEMENT_RECORD_INITIALIZE:\n    case MINIC_STATEMENT_XOR_ASSIGN:\n""",
)

# Switch-label discovery ignores leaf initialization statements just like assignments.
replace_once(
    "src/target/riscv64/codegen_statement.c",
    """        case MINIC_STATEMENT_ASSIGN:\n        case MINIC_STATEMENT_RECORD_COPY:\n        case MINIC_STATEMENT_XOR_ASSIGN:\n""",
    """        case MINIC_STATEMENT_ASSIGN:\n        case MINIC_STATEMENT_RECORD_COPY:\n        case MINIC_STATEMENT_RECORD_INITIALIZE:\n        case MINIC_STATEMENT_XOR_ASSIGN:\n""",
)
