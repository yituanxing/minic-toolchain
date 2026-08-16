from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{path}: expected {expected} matches, found {count}")
    target.write_text(text.replace(old, new))


replace_once(
    "src/frontend/ast.c",
    """    return left_array != NULL && right_array != NULL &&
           left_array->element_count == right_array->element_count &&
           left_array->is_zero_length == right_array->is_zero_length &&
           minic_c0_types_compatible(program, left_array->element_type, right_array->element_type);
""",
    """    return left_array != NULL && right_array != NULL &&
           left_array->is_zero_length == right_array->is_zero_length &&
           (left_array->element_count == right_array->element_count ||
            (!left_array->is_zero_length &&
             (left_array->element_count == 0U || right_array->element_count == 0U))) &&
           minic_c0_types_compatible(program, left_array->element_type, right_array->element_type);
""",
)

replace_once(
    "src/frontend/ast.h",
    """bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id);
""",
    """bool minic_c0_global_object_merge_declaration_type(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   MinicType declared_type);
bool minic_c0_global_object_merge_tentative(MinicC0Program *program,
                                            MinicGlobalObjectId global_object_id);
""",
)

replace_once(
    "src/frontend/ast_global.c",
    """static bool global_object_has_definition_payload(const MinicGlobalObject *object) {
""",
    """bool minic_c0_global_object_merge_declaration_type(MinicC0Program *program,
                                                   MinicGlobalObjectId global_object_id,
                                                   MinicType declared_type) {
    MinicGlobalObject *object;
    const MinicArrayType *existing_array;
    const MinicArrayType *declared_array;

    if (program == NULL || global_object_id >= program->global_object_count) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (!minic_c0_types_compatible(program, object->type, declared_type)) {
        return false;
    }
    if (!minic_type_is_array(object->type) || !minic_type_is_array(declared_type)) {
        return true;
    }
    existing_array = minic_c0_program_array_type(program, object->type.array_type_id);
    declared_array = minic_c0_program_array_type(program, declared_type.array_type_id);
    if (existing_array == NULL || declared_array == NULL) {
        return false;
    }
    if (!existing_array->is_zero_length && existing_array->element_count == 0U &&
        !declared_array->is_zero_length && declared_array->element_count != 0U) {
        return minic_c0_program_complete_array_type(
            program, object->type, declared_array->element_count);
    }
    return true;
}

static bool global_object_has_definition_payload(const MinicGlobalObject *object) {
""",
)

replace_count(
    "src/frontend/parser_function.c",
    "!minic_c0_types_compatible(parser->program, existing->type, object_type) ||",
    """!minic_c0_global_object_merge_declaration_type(
                parser->program, object_id, object_type) ||""",
    2,
)

replace_once(
    "src/frontend/parser_global.c",
    """static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    size_t element_index;

    if (array_type == NULL || array_type->element_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in array initializer")) {
        return false;
    }
    element_index = 0U;
""",
    """static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    const MinicGlobalObject *object;
    const MinicArrayType *root_array_type;
    size_t element_index;
    bool materialize_tail_zeros;

    if (array_type == NULL || array_type->element_count == 0U ||
        object_id >= parser->program->global_object_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in array initializer")) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    root_array_type = minic_type_is_array(object->type)
                          ? minic_c0_program_array_type(parser->program, object->type.array_type_id)
                          : NULL;
    materialize_tail_zeros = root_array_type != array_type;
    element_index = 0U;
""",
)

replace_once(
    "src/frontend/parser_global.c",
    """    while (element_index < array_type->element_count) {
        if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
            minic_parser_error(parser, "cannot zero-fill nested static array initializer");
            return false;
        }
        element_index += 1U;
    }
""",
    """    while (materialize_tail_zeros && element_index < array_type->element_count) {
        if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
            minic_parser_error(parser, "cannot zero-fill nested static array initializer");
            return false;
        }
        element_index += 1U;
    }
""",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """    } else {
        size_t emitted_initializer_count;

        emitted_initializer_count = object->initializer_count;
        if (minic_type_is_array(object->type)) {
            while (emitted_initializer_count != 0U &&
                   object->initializer_values[emitted_initializer_count - 1U] == 0U) {
                emitted_initializer_count -= 1U;
            }
        }
        for (initializer_index = 0U; initializer_index < emitted_initializer_count;
             ++initializer_index) {
            if (!minic_riscv64_emit_typed_bits(
                    file, program, scalar_type, object->initializer_values[initializer_index])) {
                return false;
            }
        }
        if (!minic_riscv64_emit_zero_bytes(
                file, storage_size - emitted_initializer_count * scalar_width)) {
            return false;
        }
    }
""",
    """    } else {
        for (initializer_index = 0U; initializer_index < object->initializer_count;
             ++initializer_index) {
            if (!minic_riscv64_emit_typed_bits(
                    file, program, scalar_type, object->initializer_values[initializer_index])) {
                return false;
            }
        }
        if (!minic_riscv64_emit_zero_bytes(
                file, storage_size - object->initializer_count * scalar_width)) {
            return false;
        }
    }
""",
)

source = Path("tests/compiler/c0/external_array_declarator_routing.c")
text = source.read_text()
if "completed_tentative" in text:
    raise SystemExit("external-array redeclaration regression already present")
source.write_text(
    text.rstrip()
    + """

extern unsigned long completed_tentative[];
unsigned long completed_tentative[4];

extern unsigned long completed_definition[];
unsigned long completed_definition[4] = {11, 12, 13, 14};
"""
)

replace_once(
    "tests/compiler/c0/run-external-array-declarator-routing.sh",
    "for symbol in cpu_ops empty_zero_page purgatory_sha256_digest purgatory_sha_regions initialized_map; do",
    "for symbol in cpu_ops empty_zero_page purgatory_sha256_digest purgatory_sha_regions initialized_map completed_tentative completed_definition; do",
)
