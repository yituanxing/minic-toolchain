#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()

old = r'''        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(), program, object_type,
                                    &object_size, &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            fprintf(stderr,
                    "M169_PREFLIGHT function=%s reason=object index=%zu\n",
                    function->name, index);
            return;
        }
'''
new = r'''        {
            bool is_scalar = core_scalar_type(object_type);
            bool is_record = minic_type_is_record(object_type);
            bool is_array = minic_type_is_array(object_type);
            bool has_layout = minic_data_layout_type(minic_default_data_layout(), program, object_type,
                                                     &object_size, &object_alignment);
            if ((!is_scalar && !is_record) || !has_layout ||
                (object_size == 0U && !is_record) ||
                object_alignment == 0U || object_alignment > 16U) {
                fprintf(stderr,
                        "M170_OBJECT function=%s index=%zu scalar=%d record=%d array=%d layout=%d size=%zu align=%zu elements=%zu\n",
                        function->name, index,
                        is_scalar ? 1 : 0, is_record ? 1 : 0, is_array ? 1 : 0,
                        has_layout ? 1 : 0,
                        has_layout ? object_size : 0U,
                        has_layout ? object_alignment : 0U,
                        function->objects[index].element_count);
                return;
            }
        }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M170 object detail trace: expected 1 M169 object seam, got {count}")
text = text.replace(old, new, 1)
PATH.write_text(text)
print("M170_OBJECT_DETAIL_TRACE_APPLIED")
