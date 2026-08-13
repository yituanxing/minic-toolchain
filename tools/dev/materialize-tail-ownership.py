#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file = Path(path)
    text = file.read_text()
    if old in text:
        file.write_text(text.replace(old, new, 1))
        return
    if new not in text:
        raise SystemExit(f"unexpected {label} anchor")


replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count);
""",
    """bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count);
bool minic_c0_program_discard_last_array_type(MinicC0Program *program, MinicType array_type);
""",
    "array-type API",
)

replace_once(
    "src/frontend/ast.c",
    """bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count) {
    MinicArrayType *descriptor;

    if (program == NULL || !minic_type_is_array(array_type) || element_count == 0U ||
        array_type.array_type_id >= program->array_type_count) {
        return false;
    }
    descriptor = &program->array_types[array_type.array_type_id];
    if (descriptor->is_zero_length) {
        return false;
    }
    if (descriptor->element_count != 0U) {
        return descriptor->element_count == element_count;
    }
    descriptor->element_count = element_count;
    return true;
}
""",
    """bool minic_c0_program_complete_array_type(MinicC0Program *program,
                                          MinicType array_type,
                                          size_t element_count) {
    MinicArrayType *descriptor;

    if (program == NULL || !minic_type_is_array(array_type) || element_count == 0U ||
        array_type.array_type_id >= program->array_type_count) {
        return false;
    }
    descriptor = &program->array_types[array_type.array_type_id];
    if (descriptor->is_zero_length) {
        return false;
    }
    if (descriptor->element_count != 0U) {
        return descriptor->element_count == element_count;
    }
    descriptor->element_count = element_count;
    return true;
}

bool minic_c0_program_discard_last_array_type(MinicC0Program *program, MinicType array_type) {
    MinicArrayTypeId array_type_id;

    if (program == NULL || !minic_type_is_array(array_type) || program->array_type_count == 0U) {
        return false;
    }
    array_type_id = array_type.array_type_id;
    if (array_type_id != program->array_type_count - 1U) {
        return false;
    }
    (void)memset(&program->array_types[array_type_id], 0, sizeof(program->array_types[array_type_id]));
    program->array_type_count -= 1U;
    return true;
}
""",
    "array-type implementation",
)

replace_once(
    "src/frontend/parser_function.c",
    """static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType declared_array_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot parse array parameter declarator");
        }
        return false;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL || !minic_type_pointer_to(outer_array->element_type, parameter_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    return true;
}
""",
    """static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType adjusted_type;
    MinicType declared_array_type;
    MinicType pointee_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot parse array parameter declarator");
        }
        return false;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL) {
        minic_parser_error(parser, "cannot resolve array parameter declarator");
        return false;
    }
    pointee_type = outer_array->element_type;
    if (!minic_type_pointer_to(pointee_type, &adjusted_type) ||
        !minic_c0_program_discard_last_array_type(parser->program, declared_array_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;
    return true;
}
""",
    "array-parameter adjustment",
)

codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
helper = """static bool minic_riscv64_zero_size_record_definition(const MinicC0Program *program,
                                                        const MinicGlobalObject *object) {
    const MinicRecord *record;

    if (program == NULL || object == NULL || object->storage_size != 0U ||
        !minic_type_is_record(object->type) || object->initializer_count != 0U ||
        object->relocation_count != 0U) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    return record != NULL && record->is_complete && record->field_count == 0U;
}

"""
anchor = "static bool minic_riscv64_emit_global_object(FILE *file,\n"
if helper not in text:
    if text.count(anchor) != 1:
        raise SystemExit("unexpected zero-size helper anchor")
    text = text.replace(anchor, helper + anchor, 1)

old_locals = """    unsigned int alignment_power;
    size_t scalar_width;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->storage_size == 0U || object->alignment == 0U ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }
"""
new_locals = """    unsigned int alignment_power;
    size_t scalar_width;
    size_t initializer_index;
    bool zero_size_record_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->alignment == 0U ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }
    zero_size_record_definition = minic_riscv64_zero_size_record_definition(program, object);
    if (object->storage_size == 0U && !zero_size_record_definition) {
        return false;
    }
"""
if old_locals in text:
    text = text.replace(old_locals, new_locals, 1)
elif new_locals not in text:
    raise SystemExit("unexpected zero-size global precondition")

old_record_guard = """        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || object->initializer_count == 0U) {
            return false;
        }
"""
new_record_guard = """        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_record_definition)) {
            return false;
        }
"""
if old_record_guard in text:
    text = text.replace(old_record_guard, new_record_guard, 1)
elif new_record_guard not in text:
    raise SystemExit("unexpected record initializer guard")
codegen.write_text(text)
