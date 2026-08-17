#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Semantic type queries such as typeof(array-object) sometimes need an array
# descriptor even when the source object still uses the legacy array-object
# representation. Such a descriptor can be consumed entirely by an unevaluated
# query and therefore legitimately have no persisted AST owner. Keep that
# lifetime distinction explicit instead of weakening the verifier for all
# incomplete arrays.
path = root / "src/frontend/ast.h"
text = path.read_text()
old = '''typedef struct MinicArrayType {\n    MinicType element_type;\n    size_t element_count;\n    bool is_zero_length;\n} MinicArrayType;\n'''
new = '''typedef struct MinicArrayType {\n    MinicType element_type;\n    size_t element_count;\n    bool is_zero_length;\n    bool is_query_materialized;\n} MinicArrayType;\n'''
if text.count(old) != 1:
    raise SystemExit(f"array descriptor anchor count={text.count(old)}")
text = text.replace(old, new, 1)
old = '''bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,\n                                                MinicType element_type,\n                                                MinicType *array_type);\n'''
new = old + '''bool minic_c0_program_add_query_incomplete_array_type(MinicC0Program *program,\n                                                      MinicType element_type,\n                                                      MinicType *array_type);\n'''
if text.count(old) != 1:
    raise SystemExit(f"incomplete array declaration anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = root / "src/frontend/ast.c"
text = path.read_text()
old = '''bool minic_c0_program_add_incomplete_array_type(MinicC0Program *program,\n                                                MinicType element_type,\n                                                MinicType *array_type) {\n    return minic_c0_program_add_array_descriptor(program, element_type, 0U, array_type);\n}\n'''
new = old + '''\nbool minic_c0_program_add_query_incomplete_array_type(MinicC0Program *program,\n                                                      MinicType element_type,\n                                                      MinicType *array_type) {\n    MinicType created;\n\n    if (program == NULL || array_type == NULL ||\n        !minic_c0_program_add_array_descriptor(program, element_type, 0U, &created)) {\n        return false;\n    }\n    program->array_types[created.array_type_id].is_query_materialized = true;\n    *array_type = created;\n    return true;\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f"incomplete array definition anchor count={text.count(old)}")
text = text.replace(old, new, 1)
# Completion turns the descriptor into an ordinary persistent complete array.
old = '''    descriptor->element_count = element_count;\n    return true;\n'''
new = '''    descriptor->element_count = element_count;\n    descriptor->is_query_materialized = false;\n    return true;\n'''
if text.count(old) != 1:
    raise SystemExit(f"array completion anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = root / "src/frontend/parser_postfix.c"
text = path.read_text()
old = '''    if (info.is_incomplete) {\n        return minic_c0_program_add_incomplete_array_type(\n            parser->program, info.element_type, array_type);\n    }\n'''
new = '''    if (info.is_incomplete) {\n        return minic_c0_program_add_query_incomplete_array_type(\n            parser->program, info.element_type, array_type);\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"postfix incomplete materialization anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
old = '''        if ((array_type->element_count == 0U &&\n             !incomplete_array_has_semantic_owner(program, index)) ||\n            !type_is_valid(program, target, array_type->element_type) ||\n            minic_type_is_function(array_type->element_type)) {\n'''
new = '''        if ((array_type->element_count == 0U && !array_type->is_query_materialized &&\n             !incomplete_array_has_semantic_owner(program, index)) ||\n            (array_type->is_query_materialized &&\n             (array_type->element_count != 0U || array_type->is_zero_length)) ||\n            !type_is_valid(program, target, array_type->element_type) ||\n            minic_type_is_function(array_type->element_type)) {\n'''
if text.count(old) != 1:
    raise SystemExit(f"array verifier anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
