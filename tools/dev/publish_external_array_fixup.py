#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    file_path.write_text(text.replace(old, new, 1))


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
    "flat array trailing-zero block",
)
replace_once(
    "tests/compiler/c0/run-extern-fixed-integer-arrays.sh",
    "grep -F '  .zero 2' \"$work/extern_fixed_integer_array.s\" >/dev/null",
    "test \"$(grep -Fc '  .byte 0' \"$work/extern_fixed_integer_array.s\")\" -eq 2",
    "extern fixed integer zero contract",
)
replace_once(
    "tests/compiler/c0/run-integer-constant-bitwise.sh",
    "grep -F '  .zero 3' \"$work/integer_constant_bitwise.s\" >/dev/null",
    "test \"$(grep -Fc '  .byte 0' \"$work/integer_constant_bitwise.s\")\" -eq 3",
    "integer bitwise zero contract",
)
print("staged external array typed-zero fixup")
