#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''typedef struct MinicGlobalFunctionRelocation {\n    size_t field_index;\n    MinicFunctionId function_id;\n} MinicGlobalFunctionRelocation;\n\ntypedef struct MinicGlobalObject {\n''',
    '''typedef struct MinicGlobalFunctionRelocation {\n    size_t field_index;\n    MinicFunctionId function_id;\n} MinicGlobalFunctionRelocation;\n\ntypedef struct MinicGlobalObjectRelocation {\n    size_t element_index;\n    MinicGlobalObjectId target_object_id;\n} MinicGlobalObjectRelocation;\n\ntypedef struct MinicGlobalObject {\n''',
)

# pr73-extern-object.py has already added is_extern before this script runs.
replace_once(
    "src/frontend/ast.h",
    '''    MinicGlobalFunctionRelocation function_relocations[8];\n    size_t function_relocation_count;\n    size_t storage_size;\n''',
    '''    MinicGlobalFunctionRelocation function_relocations[8];\n    size_t function_relocation_count;\n    MinicGlobalObjectRelocation *object_relocations;\n    size_t object_relocation_count;\n    size_t object_relocation_capacity;\n    size_t storage_size;\n''',
)

replace_once(
    "src/frontend/ast.h",
    '''bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,\n                                                    MinicGlobalObjectId global_object_id,\n                                                    size_t field_index,\n                                                    MinicFunctionId function_id);\n''',
    '''bool minic_c0_global_object_add_function_relocation(MinicC0Program *program,\n                                                    MinicGlobalObjectId global_object_id,\n                                                    size_t field_index,\n                                                    MinicFunctionId function_id);\nbool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  size_t element_index,\n                                                  MinicGlobalObjectId target_object_id);\n''',
)

replace_once(
    "src/frontend/ast.c",
    '''        free(program->global_objects[index].name);\n        free(program->global_objects[index].initializer_values);\n''',
    '''        free(program->global_objects[index].name);\n        free(program->global_objects[index].initializer_values);\n        free(program->global_objects[index].object_relocations);\n''',
)

# ast_global.c already has a grow_array helper used by initializer storage.
replace_once(
    "src/frontend/ast_global.c",
    '''bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id) {\n''',
    '''bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,\n                                                  MinicGlobalObjectId global_object_id,\n                                                  size_t element_index,\n                                                  MinicGlobalObjectId target_object_id) {\n    MinicGlobalObject *object;\n    MinicGlobalObjectRelocation *relocation;\n\n    if (program == NULL || global_object_id >= program->global_object_count ||\n        target_object_id >= program->global_object_count || global_object_id == target_object_id) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||\n        !grow_array((void **)&object->object_relocations,\n                    &object->object_relocation_capacity,\n                    object->object_relocation_count,\n                    sizeof(*object->object_relocations))) {\n        return false;\n    }\n    relocation = &object->object_relocations[object->object_relocation_count];\n    relocation->element_index = element_index;\n    relocation->target_object_id = target_object_id;\n    object->object_relocation_count += 1U;\n    return true;\n}\n\nbool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id) {\n''',
)

# Extern declarations cannot carry object relocations either.
replace_once(
    "src/frontend/ast_global.c",
    '''    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||\n        object->is_zero_initialized || object->is_internal) {\n''',
    '''    if (object->initializer_count != 0U || object->function_relocation_count != 0U ||\n        object->object_relocation_count != 0U || object->is_zero_initialized || object->is_internal) {\n''',
)

replace_once(
    "src/frontend/parser_internal.h",
    '''bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);\n''',
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,\n                                               MinicGlobalObjectId *object_id,\n                                               MinicType *array_type,\n                                               MinicSourceSpan *span);\nbool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);\n''',
)

# Refactor string creation so static initializers and expressions share exactly the same
# literal decoder/object construction path.
parser_string = Path("src/frontend/parser_string.c")
text = parser_string.read_text()
start = text.find("bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id) {")
if start < 0:
    raise SystemExit("parser_string.c: parse_string_literal not found")
old = text[start:]
new = r'''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    MinicSourceSpan literal_span;

    if (parser == NULL || object_id == NULL || array_type == NULL || span == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    literal_span = parser->current.span;
    if (!decoded_string_length(parser, literal_span, &decoded_length) || decoded_length == SIZE_MAX ||
        !minic_c0_program_add_array_type(
            parser->program, minic_type_char(), decoded_length + 1U, array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build string literal array type");
        }
        return false;
    }

    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_string_%zu",
                                  parser->program->global_object_count);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            *array_type,
                                            true,
                                            true,
                                            object_id) ||
        !add_string_initializers(parser, literal_span, *object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create string literal object");
        }
        return false;
    }
    *span = literal_span;
    return minic_parser_advance(parser);
}

bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourceSpan span;
    MinicType array_type;
    MinicGlobalObjectId object_id;
    MinicExpression expression;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_create_string_literal_object(parser, &object_id, &array_type, &span)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    expression.span = span;
    expression.type = array_type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.global_object_id = object_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}
'''
parser_string.write_text(text[:start] + new)

replace_once(
    "src/frontend/parser_global.c",
    '''#include <stdint.h>\n#include <string.h>\n''',
    '''#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n''',
)

# Add one-dimensional static pointer arrays with either a fixed bound or a bound inferred
# from the initializer. String literals become object relocations; null pointers remain zero.
marker = "bool minic_parser_parse_static_global(MinicParser *parser) {\n"
pg = Path("src/frontend/parser_global.c")
text = pg.read_text()
if text.count(marker) != 1:
    raise SystemExit("parser_global.c: static-global marker mismatch")
helper = r'''static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span) {
    MinicGlobalObjectId *targets;
    MinicType object_type;
    MinicType string_pointer_type;
    MinicGlobalObjectId object_id;
    size_t target_count;
    size_t target_capacity;
    size_t element_count;
    bool inferred_bound;
    bool success;

    targets = NULL;
    target_count = 0U;
    target_capacity = 0U;
    element_count = 0U;
    inferred_bound = false;
    success = false;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            goto done;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
        goto done;
    }
    if (!minic_type_pointer_to(minic_type_char(), &string_pointer_type)) {
        minic_parser_error(parser, "cannot build string pointer type");
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicGlobalObjectId target_id;

        target_id = MINIC_GLOBAL_OBJECT_INVALID;
        if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            MinicType literal_type;
            MinicSourceSpan literal_span;

            if (!minic_type_assignment_compatible(element_type, string_pointer_type) ||
                !minic_parser_create_string_literal_object(
                    parser, &target_id, &literal_type, &literal_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "string literal does not match static pointer array element");
                }
                goto done;
            }
            (void)literal_type;
            (void)literal_span;
        } else if (!parse_zero_pointer_constant(parser)) {
            goto done;
        }

        if (!inferred_bound && target_count >= element_count) {
            minic_parser_error(parser, "too many global array initializers");
            goto done;
        }
        if (target_count == target_capacity) {
            size_t new_capacity;
            MinicGlobalObjectId *resized;

            new_capacity = target_capacity == 0U ? 8U : target_capacity * 2U;
            if (new_capacity < target_capacity || new_capacity > SIZE_MAX / sizeof(*targets)) {
                minic_parser_error(parser, "too many static pointer initializers");
                goto done;
            }
            resized = (MinicGlobalObjectId *)realloc(targets, new_capacity * sizeof(*targets));
            if (resized == NULL) {
                minic_parser_error(parser, "out of memory while recording pointer initializers");
                goto done;
            }
            targets = resized;
            target_capacity = new_capacity;
        }
        targets[target_count] = target_id;
        target_count += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        goto done;
    }
    if (target_count == 0U) {
        minic_parser_error(parser, "static pointer array requires at least one initializer");
        goto done;
    }
    if (inferred_bound) {
        element_count = target_count;
    }

    if (!minic_c0_program_add_array_type(parser->program, element_type, element_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot create static pointer array object");
        goto done;
    }
    {
        size_t index;

        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program, object_id, index, targets[index])) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
    }
    success = minic_parser_expect(parser,
                                  MINIC_TOKEN_SEMICOLON,
                                  "expected ';' after global object");

done:
    free(targets);
    return success;
}

'''
pg.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_global.c",
    '''    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {\n        return parse_static_scalar(parser, element_type, name_span);\n    }\n    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {\n''',
    '''    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {\n        return parse_static_scalar(parser, element_type, name_span);\n    }\n    if (minic_type_is_pointer(element_type)) {\n        return parse_static_pointer_array(parser, element_type, name_span);\n    }\n    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {\n''',
)

# Verify relocation storage and targets without relying on target layout, which is filled later.
replace_once(
    "src/frontend/ast_verifier.c",
    '''            (object->is_extern &&\n             (object->is_internal || object->is_zero_initialized ||\n              object->initializer_count != 0U || object->function_relocation_count != 0U)) ||\n            (object->is_zero_initialized && object->initializer_count != 0U) ||\n            !storage_is_valid(object->initializer_values,\n                              object->initializer_count,\n                              object->initializer_capacity)) {\n            return false;\n        }\n''',
    '''            (object->is_extern &&\n             (object->is_internal || object->is_zero_initialized ||\n              object->initializer_count != 0U || object->function_relocation_count != 0U ||\n              object->object_relocation_count != 0U)) ||\n            (object->is_zero_initialized && object->initializer_count != 0U) ||\n            (object->object_relocation_count != 0U &&\n             (!object->is_zero_initialized || object->function_relocation_count != 0U ||\n              object->initializer_count != 0U)) ||\n            !storage_is_valid(object->initializer_values,\n                              object->initializer_count,\n                              object->initializer_capacity) ||\n            !storage_is_valid(object->object_relocations,\n                              object->object_relocation_count,\n                              object->object_relocation_capacity)) {\n            return false;\n        }\n        {\n            size_t relocation_index;\n\n            for (relocation_index = 0U; relocation_index < object->object_relocation_count;\n                 ++relocation_index) {\n                if (object->object_relocations[relocation_index].target_object_id >=\n                    program->global_object_count) {\n                    return false;\n                }\n            }\n        }\n''',
)

# Emit zero-backed pointer arrays with sparse object address relocations.
codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
marker = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
if text.count(marker) != 1:
    raise SystemExit("codegen_function.c: global emitter marker mismatch")
object_emitter = r'''static bool emit_object_relocs(FILE *file,
                               const MinicC0Program *program,
                               const MinicGlobalObject *object) {
    const MinicArrayType *array_type;
    size_t cursor_element;
    size_t relocation_index;

    if (file == NULL || program == NULL || object == NULL || !object->is_zero_initialized ||
        object->object_relocation_count == 0U || object->initializer_count != 0U ||
        object->function_relocation_count != 0U || !minic_type_is_array(object->type)) {
        return false;
    }
    array_type = minic_c0_program_array_type(program, object->type.array_type_id);
    if (array_type == NULL || !minic_type_is_pointer(array_type->element_type) ||
        object->storage_size != array_type->element_count * 8U) {
        return false;
    }

    cursor_element = 0U;
    for (relocation_index = 0U; relocation_index < object->object_relocation_count;
         ++relocation_index) {
        const MinicGlobalObjectRelocation *relocation;
        const MinicGlobalObject *target;

        relocation = &object->object_relocations[relocation_index];
        target = minic_c0_program_global_object(program, relocation->target_object_id);
        if (target == NULL || target->name_length == 0U ||
            relocation->element_index < cursor_element ||
            relocation->element_index >= array_type->element_count ||
            !minic_riscv64_emit_zero_bytes(
                file, (relocation->element_index - cursor_element) * 8U) ||
            fprintf(file, "  .dword %s\n", target->name) < 0) {
            return false;
        }
        cursor_element = relocation->element_index + 1U;
    }
    return cursor_element <= array_type->element_count &&
           minic_riscv64_emit_zero_bytes(
               file, (array_type->element_count - cursor_element) * 8U);
}

'''
codegen.write_text(text.replace(marker, object_emitter + marker, 1))

replace_once(
    "src/target/riscv64/codegen_function.c",
    '''    if (object->function_relocation_count != 0U) {\n        if (!emit_fn_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->is_zero_initialized) {\n''',
    '''    if (object->function_relocation_count != 0U) {\n        if (!emit_fn_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->object_relocation_count != 0U) {\n        if (!emit_object_relocs(file, program, object)) {\n            return false;\n        }\n    } else if (object->is_zero_initialized) {\n''',
)

print("staged static pointer arrays with string object relocations")
