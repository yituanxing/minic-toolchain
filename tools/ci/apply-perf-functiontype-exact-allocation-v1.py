#!/usr/bin/env python3
"""Single-factor disposable AST FunctionType signature storage trial.

After Function param exact storage, FunctionType still embeds 64*72 bytes
for each deduplicated type. This prototype owns exactly parameter_count
elements per FunctionType; all consumers preserve pointer[index] syntax.
Do NOT apply to production until frozen500 and runtime evidence support it.
"""
from pathlib import Path
def once(path,old,new):
    p=Path(path);s=p.read_text();n=s.count(old)
    if n!=1:raise SystemExit(f"FUNCTION_TYPE_EXACT_ANCHOR_MISMATCH {path} {n} {old[:80]!r}")
    p.write_text(s.replace(old,new,1))

once("src/frontend/ast.h",
     """typedef struct MinicFunctionType {
    MinicType return_type;
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_count;
    bool is_variadic;
} MinicFunctionType;""",
     """typedef struct MinicFunctionType {
    MinicType return_type;
    /* Program-owned private exact-length signature; no aliasing to parser
       temporaries or other FunctionType entries after array reallocation. */
    MinicType *parameter_types;
    size_t parameter_count;
    bool is_variadic;
} MinicFunctionType;""")

once("src/frontend/ast.c",
     """    for (index = 0U; index < program->type_alias_count; ++index) {
        free(program->type_aliases[index].name);
    }""",
     """    for (index = 0U; index < program->function_type_count; ++index) {
        free(program->function_types[index].parameter_types);
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        free(program->type_aliases[index].name);
    }""")

once("src/frontend/ast.c",
     """    descriptor.return_type = return_type;
    descriptor.parameter_count = parameter_count;
    descriptor.is_variadic = is_variadic;
    for (parameter_index = 0U; parameter_index < MINIC_MAX_FUNCTION_PARAMETERS; ++parameter_index) {
        if (parameter_index < parameter_count) {
            descriptor.parameter_types[parameter_index] =
                normalized_parameter_types[parameter_index];
        } else {
            descriptor.parameter_types[parameter_index] = minic_type_void();
        }
    }
    function_type_index = program->function_type_count;""",
     """    descriptor.return_type = return_type;
    descriptor.parameter_count = parameter_count;
    descriptor.is_variadic = is_variadic;
    /* Allocate only after normalization/deduplication; never point into the
       parser's temporary signature, which may die before later passes. */
    if (parameter_count != 0U) {
        if (parameter_count > SIZE_MAX / sizeof(*descriptor.parameter_types)) {
            return false;
        }
        descriptor.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*descriptor.parameter_types));
        if (descriptor.parameter_types == NULL) {
            return false;
        }
        (void)memcpy(descriptor.parameter_types, normalized_parameter_types,
                     parameter_count * sizeof(*descriptor.parameter_types));
    }
    function_type_index = program->function_type_count;""")
print("MINIC_FUNCTION_TYPE_EXACT=APPLIED fields=unchanged except param_pointer")
