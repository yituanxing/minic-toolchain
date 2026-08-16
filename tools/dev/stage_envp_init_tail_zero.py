from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/riscv64/codegen_function.c",
    """        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t element_emitted;

            if (!minic_riscv64_emit_constant_value(file,
""",
    """        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            size_t element_emitted;

            if (*initializer_index == object->initializer_count &&
                *relocation_index == object->relocation_count) {
                if (cursor > type_size ||
                    !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
                    return false;
                }
                *emitted_size = type_size;
                return true;
            }
            if (!minic_riscv64_emit_constant_value(file,
""",
)

source = Path("tests/compiler/c0/external_pointer_array.c")
text = source.read_text()
if "envp_init" in text:
    raise SystemExit("envp_init regression already present")
source.write_text(
    text.rstrip()
    + """

const char *envp_init[32 + 2] = {
    "HOME=/",
    "TERM=linux",
    (void *)0,
};
"""
)

replace_once(
    "tests/compiler/c0/run-external-pointer-arrays.sh",
    """grep -F '.size names, 32' "$asm" >/dev/null
printf '%s\\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 storage=32'
""",
    """grep -F '.size names, 32' "$asm" >/dev/null
grep -F '.type envp_init, @object' "$asm" >/dev/null
grep -F '.globl envp_init' "$asm" >/dev/null
grep -F '  .zero 248' "$asm" >/dev/null
grep -F '.size envp_init, 272' "$asm" >/dev/null
printf '%s\\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 envp-tail-zero=31 storage=32,272'
""",
)
