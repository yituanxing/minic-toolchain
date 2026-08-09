#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one global anchor, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_in_function(path: str, signature: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"{label}: missing function {signature}")
    candidates = [
        position
        for position in (
            text.find("\nstatic ", start + len(signature)),
            text.find("\nbool ", start + len(signature)),
            text.find("\nvoid ", start + len(signature)),
            text.find("\nsize_t ", start + len(signature)),
        )
        if position >= 0
    ]
    end = min(candidates) if candidates else len(text)
    body = text[start:end]
    count = body.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor in {signature}, found {count}")
    target.write_text(text[:start] + body.replace(old, new, 1) + text[end:])


replace_in_function(
    "src/frontend/ast_verifier.c",
    "static bool type_is_valid(",
    """                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
""",
    """                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_INT128);
""",
    "int128-verifier",
)

replace_in_function(
    "src/frontend/parser_core.c",
    "static bool array_bound_apply_integer_cast(",
    """    case MINIC_INTEGER_RANK_LONG:
    case MINIC_INTEGER_RANK_LONG_LONG:
        bits = 64U;
        break;
    case MINIC_INTEGER_RANK_NONE:
""",
    """    case MINIC_INTEGER_RANK_LONG:
    case MINIC_INTEGER_RANK_LONG_LONG:
        bits = 64U;
        break;
    case MINIC_INTEGER_RANK_INT128:
        minic_parser_error(parser, "128-bit cast exceeds the current 64-bit constant evaluator");
        return false;
    case MINIC_INTEGER_RANK_NONE:
""",
    "int128-array-bound-cast",
)

# constant_type_layout has a forward declaration earlier in parser_core.c. Patch the
# unique switch arm globally so we do not mistake that declaration for the definition.
replace_once(
    "src/frontend/parser_core.c",
    """        case MINIC_INTEGER_RANK_LONG:
        case MINIC_INTEGER_RANK_LONG_LONG:
            *size = 8U;
            *alignment = 8U;
            return true;
        case MINIC_INTEGER_RANK_NONE:
""",
    """        case MINIC_INTEGER_RANK_LONG:
        case MINIC_INTEGER_RANK_LONG_LONG:
            *size = 8U;
            *alignment = 8U;
            return true;
        case MINIC_INTEGER_RANK_INT128:
            *size = 16U;
            *alignment = 16U;
            return true;
        case MINIC_INTEGER_RANK_NONE:
""",
    "int128-constant-layout",
)

print("staged GNU int128 AST contracts and RV64 constant layout")
