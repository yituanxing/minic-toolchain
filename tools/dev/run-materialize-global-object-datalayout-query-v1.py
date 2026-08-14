#!/usr/bin/env python3
from pathlib import Path

primary = Path("tools/dev/materialize-global-object-datalayout-query-v1.py")
source = primary.read_text(encoding="utf-8")
exec(compile(source, str(primary), "exec"), {"__name__": "__main__", "__file__": str(primary)})

path = Path("tests/target/riscv64/layout_test.c")
text = path.read_text(encoding="utf-8")
start_marker = """    {
        MinicGlobalObjectId aligned_id;
"""
end_marker = """    minic_c0_program_destroy(&program);
    (void)printf("PASS target/riscv64/layout\\n");
"""
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("generated global-object query test block not found")
replacement = """    {
        MinicGlobalObject object;
        MinicRecordId incomplete_record_id;
        MinicType incomplete_array_type;
        size_t size;
        size_t alignment;

        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_int();
        object.explicit_alignment = 16U;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 4U || alignment != 16U) {
            minic_c0_program_destroy(&program);
            return fail("explicit global object alignment query");
        }

        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_void();
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern void object layout query");
        }

        if (!minic_c0_program_add_record(&program, "Incomplete", 10U, &incomplete_record_id)) {
            minic_c0_program_destroy(&program);
            return fail("construct incomplete record type");
        }
        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_record(incomplete_record_id);
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete record layout query");
        }

        if (!minic_c0_program_add_incomplete_array_type(
                &program, minic_type_int(), &incomplete_array_type)) {
            minic_c0_program_destroy(&program);
            return fail("construct incomplete array type");
        }
        (void)memset(&object, 0, sizeof(object));
        object.type = incomplete_array_type;
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete array layout query");
        }
    }

"""
text = text[:start] + replacement + text[end:]
path.write_text(text, encoding="utf-8")
print("NORMALIZED global-object-datalayout-query-v1 focused test")
