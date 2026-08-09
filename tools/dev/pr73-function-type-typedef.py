#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


# A declaration such as `typedef void(callback)(int);` names a function type.
# The existing parenthesized typedef parser already builds the function descriptor;
# only its artificial requirement for at least one '*' prevented this standard form.
replace_once(
    "src/frontend/parser_typedef.c",
    '''    if (pointer_depth == 0U) {\n        minic_parser_error(parser, "function pointer typedef requires '*'");\n        return false;\n    }\n''',
    '',
)

# Function types are valid typedef targets. They are still not object types; users must
# apply a pointer declarator before declaring an object or parameter of that type.
replace_once(
    "src/frontend/ast.c",
    '''    if (program == NULL || name == NULL || alias_id == NULL || minic_type_is_void(type) ||\n        minic_type_is_function(type)) {\n''',
    '''    if (program == NULL || name == NULL || alias_id == NULL || minic_type_is_void(type)) {\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''    for (index = 0U; index < program->type_alias_count; ++index) {\n        if (program->type_aliases[index].name == NULL ||\n            !type_is_valid(program, program->type_aliases[index].type) ||\n            minic_type_is_function(program->type_aliases[index].type)) {\n            return false;\n        }\n    }\n''',
    '''    for (index = 0U; index < program->type_alias_count; ++index) {\n        if (program->type_aliases[index].name == NULL ||\n            !type_is_valid(program, program->type_aliases[index].type)) {\n            return false;\n        }\n    }\n''',
)

print("staged function type typedef aliases")
