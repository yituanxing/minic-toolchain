#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/type.h",
    "    unsigned int pointer_qualifiers;\n    unsigned int pointer_depth;\n",
    "    unsigned int pointer_qualifiers;\n    unsigned int pointer_volatile_qualifiers;\n    unsigned int pointer_depth;\n",
)

replace_once(
    "src/frontend/type.c",
    '''    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U;
''',
    '''    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U &&
           (type.pointer_volatile_qualifiers >> type.pointer_depth) == 0U;
''',
)

replace_once(
    "src/frontend/type.c",
    '''    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static MinicType minic_type_scalar''',
    '''    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static MinicType minic_type_scalar''',
)
replace_once(
    "src/frontend/type.c",
    '''    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static bool minic_type_same_unqualified_identity''',
    '''    type.base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_volatile_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    type.pointer_depth = 0U;
    return type;
}

static bool minic_type_same_unqualified_identity''',
)

replace_once(
    "src/frontend/type.c",
    '''bool minic_type_add_volatile(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type) ||
        minic_type_is_function(type) || type.pointer_depth != 0U) {
        return false;
    }
    *result = type;
    result->base_qualifiers |= MINIC_TYPE_QUALIFIER_VOLATILE;
    return true;
}
''',
    '''bool minic_type_add_volatile(MinicType type, MinicType *result) {
    if (result == NULL || !minic_type_pointer_qualifiers_are_valid(type) ||
        minic_type_is_function(type)) {
        return false;
    }
    *result = type;
    if (type.pointer_depth == 0U) {
        result->base_qualifiers |= MINIC_TYPE_QUALIFIER_VOLATILE;
    } else {
        result->pointer_volatile_qualifiers |= 1U << (type.pointer_depth - 1U);
    }
    return true;
}
''',
)

replace_once(
    "src/frontend/type.c",
    '''    if (type.pointer_depth == 0U) {
        result->base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    } else {
        result->pointer_qualifiers &= ~(1U << (type.pointer_depth - 1U));
    }
''',
    '''    if (type.pointer_depth == 0U) {
        result->base_qualifiers = MINIC_TYPE_QUALIFIER_NONE;
    } else {
        result->pointer_qualifiers &= ~(1U << (type.pointer_depth - 1U));
        result->pointer_volatile_qualifiers &= ~(1U << (type.pointer_depth - 1U));
    }
''',
)

replace_once(
    "src/frontend/type.c",
    '''    result->pointer_qualifiers &= ~(1U << removed_level);
    result->pointer_depth -= 1U;
''',
    '''    result->pointer_qualifiers &= ~(1U << removed_level);
    result->pointer_volatile_qualifiers &= ~(1U << removed_level);
    result->pointer_depth -= 1U;
''',
)

replace_once(
    "src/frontend/type.c",
    '''    return minic_type_same_unqualified_identity(left, right) &&
           left.base_qualifiers == right.base_qualifiers &&
           left.pointer_qualifiers == right.pointer_qualifiers;
''',
    '''    return minic_type_same_unqualified_identity(left, right) &&
           left.base_qualifiers == right.base_qualifiers &&
           left.pointer_qualifiers == right.pointer_qualifiers &&
           left.pointer_volatile_qualifiers == right.pointer_volatile_qualifiers;
''',
)

replace_once(
    "src/frontend/type.c",
    '''bool minic_type_is_volatile(MinicType type) {
    return minic_type_pointer_qualifiers_are_valid(type) && type.pointer_depth == 0U &&
           (type.base_qualifiers & MINIC_TYPE_QUALIFIER_VOLATILE) != 0U;
}
''',
    '''bool minic_type_is_volatile(MinicType type) {
    if (!minic_type_pointer_qualifiers_are_valid(type)) {
        return false;
    }
    if (type.pointer_depth == 0U) {
        return (type.base_qualifiers & MINIC_TYPE_QUALIFIER_VOLATILE) != 0U;
    }
    return (type.pointer_volatile_qualifiers & (1U << (type.pointer_depth - 1U))) != 0U;
}
''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U;
''',
    '''    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U &&
           (type.pointer_volatile_qualifiers >> type.pointer_depth) == 0U;
''',
)

replace_once(
    "src/frontend/parser_type.c",
    '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
               parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
            if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
                minic_parser_error(parser, "pointer volatile qualifier is not supported yet");
                return false;
            }
            if (!minic_type_add_const(parsed_type, &parsed_type)) {
                minic_parser_error(parser, "cannot apply pointer const qualifier");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
''',
    '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
               parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
            if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
                if (!minic_type_add_volatile(parsed_type, &parsed_type)) {
                    minic_parser_error(parser, "cannot apply pointer volatile qualifier");
                    return false;
                }
            } else if (!minic_type_add_const(parsed_type, &parsed_type)) {
                minic_parser_error(parser, "cannot apply pointer const qualifier");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
''',
)

print("staged per-pointer-level volatile qualifiers")
