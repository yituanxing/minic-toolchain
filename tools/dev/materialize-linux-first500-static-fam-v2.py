#!/usr/bin/env python3
"""Run the static-FAM materializer with current emitter-safe anchors."""
from pathlib import Path

source_path = Path("tools/dev/materialize-linux-first500-static-fam-v1.py")
source = source_path.read_text()
old = '''replace_once(
    codegen,
    """        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
""",
    """        if (cursor > record_storage_size ||
            !minic_riscv64_emit_zero_bytes(file, record_storage_size - cursor)) {
            return false;
        }
        *emitted_size = record_storage_size;
        return true;
""",
)
'''
new = '''replace_once(
    codegen,
    """        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}
""",
    """        if (cursor > record_storage_size ||
            !minic_riscv64_emit_zero_bytes(file, record_storage_size - cursor)) {
            return false;
        }
        *emitted_size = record_storage_size;
        return true;
    }
    return false;
}
""",
)
'''
if source.count(old) != 1:
    raise SystemExit("static-FAM v1 emitter replacement block changed unexpectedly")
source = source.replace(old, new, 1)

shadowed = '''            if (field->is_flexible_array) {
                size_t element_index;
                size_t field_offset;
                size_t flexible_element_count;
'''
unshadowed = '''            if (field->is_flexible_array) {
                size_t flexible_element_count;
'''
if source.count(shadowed) != 1:
    raise SystemExit("static-FAM v1 flexible emitter locals changed unexpectedly")
source = source.replace(shadowed, unshadowed, 1)

exec(compile(source, str(source_path), "exec"), {"__name__": "__main__"})
