#!/usr/bin/env python3
"""Experimental Function parameter-type cold storage (disposable CI checkout).

This is intentionally NOT the proposed four-inline final design. It is an
isolated representation A/B measuring the benefit/cost of exact-length
separate storage without touching Specialization arrays or FunctionType.
Direct field[index] reads and by-value Function snapshots still compile.
Signature setter owns one private copy per Function; specialization clone
copies through existing minic_c0_program_set_function_signature() semantics.
"""
from pathlib import Path

def once(path,old,new):
    p=Path(path)
    s=p.read_text()
    n=s.count(old)
    if n!=1:
        raise SystemExit(f"MINIC_FUNCTION_PARAM_ALLOC=ANCHOR_MISMATCH {path} n={n} {old[:90]!r}")
    p.write_text(s.replace(old,new,1))

once("src/frontend/ast.h",
     "    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];\n    size_t parameter_count;\n    size_t local_begin;",
     """    /* Prototype: private exact-length allocation, owned by this Program
       Function. Value-copy snapshots borrow it, never free or modify. */
    MinicType *parameter_types;
    size_t parameter_count;
    size_t local_begin;""")

once("src/frontend/ast.c",
     """        free(program->functions[index].section_name);
    }
    for (index = 0U; index < program->record_count; ++index) {""",
     """        free(program->functions[index].section_name);
        free(program->functions[index].parameter_types);
    }
    for (index = 0U; index < program->record_count; ++index) {""")

once("src/frontend/ast.c",
     """void minic_c0_function_initialize_defaults(MinicFunction *function) {
    size_t parameter_index;

    if (function == NULL) {
        return;
    }
    (void)memset(function, 0, sizeof(*function));
    function->return_type = minic_type_int();
    for (parameter_index = 0U;
         parameter_index < sizeof(function->parameter_types) / sizeof(function->parameter_types[0]);
         ++parameter_index) {
        function->parameter_types[parameter_index] = minic_type_int();
    }
    function->alias_target = MINIC_FUNCTION_INVALID;
}""",
     """void minic_c0_function_initialize_defaults(MinicFunction *function) {
    if (function == NULL) {
        return;
    }
    (void)memset(function, 0, sizeof(*function));
    function->return_type = minic_type_int();
    function->alias_target = MINIC_FUNCTION_INVALID;
}""")

once("src/frontend/ast.c",
     """    MinicFunction *function;
    MinicType normalized_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_index;

    if (program == NULL || function_id >= program->function_count ||""",
     """    MinicFunction *function;
    MinicType normalized_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType *private_parameter_types = NULL;
    size_t parameter_index;

    if (program == NULL || function_id >= program->function_count ||""")

once("src/frontend/ast.c",
     """    function->return_type = return_type;
    function->parameter_count = parameter_count;
    for (parameter_index = 0U; parameter_index < MINIC_MAX_FUNCTION_PARAMETERS; ++parameter_index) {
        if (parameter_index < parameter_count) {
            function->parameter_types[parameter_index] =
                normalized_parameter_types[parameter_index];
        } else {
            function->parameter_types[parameter_index] = minic_type_void();
        }
    }
    return true;
}

bool minic_c0_program_set_function_parameter_count""",
     """    /* Copy BEFORE freeing old storage, including the alias-safe case
       where parameter_types points at this Function's previous signature.
       Never leave a half-updated Program Function on OOM. */
    if (parameter_count != 0U) {
        if (parameter_count > SIZE_MAX / sizeof(*private_parameter_types)) {
            return false;
        }
        private_parameter_types = malloc(parameter_count * sizeof(*private_parameter_types));
        if (private_parameter_types == NULL) {
            return false;
        }
        (void)memcpy(private_parameter_types, normalized_parameter_types,
                     parameter_count * sizeof(*private_parameter_types));
    }
    free(function->parameter_types);
    function->parameter_types = private_parameter_types;
    function->return_type = return_type;
    function->parameter_count = parameter_count;
    return true;
}

bool minic_c0_program_set_function_parameter_count""")

print("MINIC_FUNCTION_PARAM_ALLOC=APPLIED only_ast_header_and_ast_c=true ownership=per_function")
