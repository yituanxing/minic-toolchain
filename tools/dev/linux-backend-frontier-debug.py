#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
old = """        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
        }
"""
new = """        success =
            minic_riscv64_emit_global_object(file, program, &program->global_objects[global_index]);
        if (!success) {
            const MinicGlobalObject *failed_object = &program->global_objects[global_index];
            (void)fprintf(stderr,
                          \"MINIC_RV64_GLOBAL_FAIL index=%zu name=%s init=%zu reloc=%zu zero=%d\\n\",
                          global_index,
                          failed_object->name,
                          failed_object->initializer_count,
                          failed_object->relocation_count,
                          failed_object->is_zero_initialized ? 1 : 0);
        }
"""
count = text.count(old)
if count != 1:
    raise SystemExit(f"RV64 diagnostic anchor count={count}")
path.write_text(text.replace(old, new, 1))
