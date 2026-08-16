#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()

begin = text.index("static bool\nminic_riscv64_integer_storage_width")
end = text.index("static const char *minic_riscv64_integer_data_directive", begin)
text = text[:begin] + text[end:]

text = replace_once(
    text,
    '''    MinicType type;

    if (program == NULL || scalar_type == NULL || scalar_width == NULL) {
''',
    '''    MinicType type;
    size_t alignment;

    if (program == NULL || scalar_type == NULL || scalar_width == NULL) {
''',
    "global scalar alignment declaration",
)
text = replace_once(
    text,
    '''    if (!minic_riscv64_integer_storage_width(program, type, scalar_width)) {
        return false;
    }
    *scalar_type = type;
''',
    '''    if ((!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        !minic_riscv64_type_layout(program, type, scalar_width, &alignment) ||
        (*scalar_width != 1U && *scalar_width != 2U && *scalar_width != 4U &&
         *scalar_width != 8U)) {
        return false;
    }
    (void)alignment;
    *scalar_type = type;
''',
    "global scalar integer-or-pointer layout",
)

text = replace_once(
    text,
    "static bool minic_riscv64_emit_record_array_values(FILE *file,",
    "static bool minic_riscv64_emit_recursive_array_values(FILE *file,",
    "recursive array emitter name",
)
text = replace_once(
    text,
    '''    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        !minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
        !minic_data_layout_global_object(
''',
    '''    if (file == NULL || program == NULL || object == NULL || object->is_zero_initialized ||
        !minic_type_is_array(object->type) ||
        !minic_data_layout_global_object(
''',
    "recursive array emitter guard",
)
text = replace_once(
    text,
    '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL)) {
        if (object->initializer_count == 0U) {
            return false;
        }
    } else {
        if (object->relocation_count != 0U ||
''',
    '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
               (minic_type_is_array(object->type) && object->relocation_count != 0U)) {
        if (object->initializer_count == 0U) {
            return false;
        }
    } else {
        if (object->relocation_count != 0U ||
''',
    "relocation-bearing array validation",
)
text = replace_once(
    text,
    '''    } else if (minic_riscv64_record_array_info(program, object->type, NULL, NULL) &&
               object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_array_values(file, program, object)) {
            return false;
        }
''',
    '''    } else if ((minic_riscv64_record_array_info(program, object->type, NULL, NULL) ||
                (minic_type_is_array(object->type) && object->relocation_count != 0U)) &&
               object->initializer_count != 0U) {
        if (!minic_riscv64_emit_recursive_array_values(file, program, object)) {
            return false;
        }
''',
    "relocation-bearing array emission dispatch",
)
path.write_text(text)

runner = Path("tests/compiler/c0/run-external-pointer-arrays.sh")
data = runner.read_text()
data = replace_once(
    data,
    '''grep -F '  .zero 8' "$asm" >/dev/null
''',
    '''grep -F '  .dword 0' "$asm" >/dev/null
''',
    "canonical pointer null slot spelling",
)
runner.write_text(data)
print("staged relocation-aware array static-data emission")
