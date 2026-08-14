#!/usr/bin/env python3
from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# Public DataLayout object-level query.
path = "src/target/data_layout.h"
text = read(path)
anchor = """bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment);
"""
insert = anchor + """bool minic_data_layout_global_object(const MinicDataLayout *layout,
                                     const MinicC0Program *program,
                                     const MinicGlobalObject *object,
                                     size_t *size,
                                     size_t *alignment);
"""
text = replace_once(text, anchor, insert, "data_layout.h global object query")
write(path, text)

path = "src/target/data_layout.c"
text = read(path)
anchor = """bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment) {
    return minic_data_layout_type_depth(layout, program, type, 0U, size, alignment);
}

"""
insert = anchor + """bool minic_data_layout_global_object(const MinicDataLayout *layout,
                                     const MinicC0Program *program,
                                     const MinicGlobalObject *object,
                                     size_t *size,
                                     size_t *alignment) {
    size_t object_size;
    size_t object_alignment;

    if (layout == NULL || program == NULL || object == NULL || size == NULL || alignment == NULL) {
        return false;
    }
    if (object->is_extern && minic_type_is_void(object->type)) {
        *size = 0U;
        *alignment = 0U;
        return true;
    }
    if (object->is_extern && minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        if (record != NULL && !record->is_complete) {
            *size = 0U;
            *alignment = 0U;
            return true;
        }
    }
    if (object->is_extern && minic_type_is_array(object->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        if (array_type != NULL && array_type->element_count == 0U) {
            *size = 0U;
            *alignment = 0U;
            return true;
        }
    }
    if (!minic_data_layout_type(layout, program, object->type, &object_size, &object_alignment)) {
        return false;
    }
    if (object->explicit_alignment != 0U) {
        if ((object->explicit_alignment & (object->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (object->explicit_alignment > object_alignment) {
            object_alignment = object->explicit_alignment;
        }
    }
    *size = object_size;
    *alignment = object_alignment;
    return true;
}

"""
text = replace_once(text, anchor, insert, "data_layout.c global object query")
write(path, text)

# RV64 layout delegates object policy to DataLayout and only mirrors the result for now.
path = "src/target/riscv64/layout.c"
text = read(path)
start = text.index("static bool minic_riscv64_layout_globals(")
end = text.index("void minic_riscv64_function_layout_initialize(", start)
replacement = """static bool minic_riscv64_layout_globals(MinicC0Program *program) {
    size_t object_index;

    if (program == NULL) {
        return false;
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        MinicGlobalObject *object;
        size_t storage_size;
        size_t alignment;

        object = &program->global_objects[object_index];
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), program, object, &storage_size, &alignment)) {
            return false;
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
    }
    return true;
}

"""
text = text[:start] + replacement + text[end:]
write(path, text)

# Focused regression: exercise object policy independently of the RV64 mirror pass.
path = "tests/target/riscv64/layout_test.c"
text = read(path)
anchor = """    minic_c0_program_destroy(&program);
    (void)printf("PASS target/riscv64/layout\\n");
    return 0;
}
"""
insert = """    {
        MinicGlobalObjectId aligned_id;
        MinicGlobalObjectId extern_void_id;
        MinicGlobalObjectId extern_record_id;
        MinicGlobalObjectId extern_array_id;
        MinicRecordId incomplete_record_id;
        MinicType incomplete_array_type;
        const MinicGlobalObject *object;
        size_t size;
        size_t alignment;

        if (!minic_c0_program_add_global_object(
                &program, "aligned_global", 14U, minic_type_int(), false, false, &aligned_id) ||
            !minic_c0_global_object_set_explicit_alignment(&program, aligned_id, 16U)) {
            minic_c0_program_destroy(&program);
            return fail("construct explicitly aligned global object");
        }
        object = minic_c0_program_global_object(&program, aligned_id);
        if (object == NULL ||
            !minic_data_layout_global_object(
                minic_default_data_layout(), &program, object, &size, &alignment) ||
            size != 4U || alignment != 16U) {
            minic_c0_program_destroy(&program);
            return fail("explicit global object alignment query");
        }

        if (!minic_c0_program_add_extern_global_object(
                &program, "extern_void", 11U, minic_type_void(), false, &extern_void_id)) {
            minic_c0_program_destroy(&program);
            return fail("construct extern void object");
        }
        object = minic_c0_program_global_object(&program, extern_void_id);
        if (object == NULL ||
            !minic_data_layout_global_object(
                minic_default_data_layout(), &program, object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern void object layout query");
        }

        if (!minic_c0_program_add_record(
                &program, "Incomplete", 10U, &incomplete_record_id) ||
            !minic_c0_program_add_extern_global_object(&program,
                                                       "extern_record",
                                                       13U,
                                                       minic_type_record(incomplete_record_id),
                                                       false,
                                                       &extern_record_id)) {
            minic_c0_program_destroy(&program);
            return fail("construct extern incomplete record object");
        }
        object = minic_c0_program_global_object(&program, extern_record_id);
        if (object == NULL ||
            !minic_data_layout_global_object(
                minic_default_data_layout(), &program, object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete record layout query");
        }

        if (!minic_c0_program_add_incomplete_array_type(
                &program, minic_type_int(), &incomplete_array_type) ||
            !minic_c0_program_add_extern_global_object(&program,
                                                       "extern_array",
                                                       12U,
                                                       incomplete_array_type,
                                                       false,
                                                       &extern_array_id)) {
            minic_c0_program_destroy(&program);
            return fail("construct extern incomplete array object");
        }
        object = minic_c0_program_global_object(&program, extern_array_id);
        if (object == NULL ||
            !minic_data_layout_global_object(
                minic_default_data_layout(), &program, object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete array layout query");
        }
    }

""" + anchor
text = replace_once(text, anchor, insert, "layout test global object query")
write(path, text)

print("MATERIALIZED global-object-datalayout-query-v1")
