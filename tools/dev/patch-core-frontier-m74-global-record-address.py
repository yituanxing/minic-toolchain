#!/usr/bin/env python3
"""Allow Core IR to form addresses of global record objects."""

from pathlib import Path

MARKER = "M74_GLOBAL_RECORD_ADDRESS"

FILES = (
    Path("src/core/core_lower.c"),
    Path("src/core/core_ir.c"),
    Path("src/target/riscv64/core_codegen.c"),
)


def patch_file(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        print(f"M74 already applied: {path}")
        return

    if path == Path("src/core/core_lower.c"):
        anchor = '''static bool core_global_addressable_type(MinicType type) {
    return core_memory_scalar_type(type) || minic_type_is_array(type);
}
'''
        replacement = '''/* M74_GLOBAL_RECORD_ADDRESS: an object need not be scalar to have an
   address. Core field-address lowering already consumes pointers to records;
   permit global record objects to enter that path just like arrays. */
static bool core_global_addressable_type(MinicType type) {
    return core_memory_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type);
}
'''
    elif path == Path("src/core/core_ir.c"):
        anchor = '''static bool core_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_array(type);
}
'''
        replacement = '''/* M74_GLOBAL_RECORD_ADDRESS: global.addr is an address-forming Core
   primitive. Record globals are addressable even though record values are not
   scalar SSA values. */
static bool core_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_array(type) || minic_type_is_record(type);
}
'''
    elif path == Path("src/target/riscv64/core_codegen.c"):
        anchor = '''static bool core_global_addressable_type(MinicType type) {
    return core_scalar_type(type) || minic_type_is_array(type);
}
'''
        replacement = '''/* M74_GLOBAL_RECORD_ADDRESS: RV64 global.addr lowers to `la symbol`;
   the pointee's aggregate shape is irrelevant until field/element addressing. */
static bool core_global_addressable_type(MinicType type) {
    return core_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type);
}
'''
    else:
        raise AssertionError(path)

    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"M74 anchor count={count} in {path}")
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"M74 global record address applied: {path}")


def main() -> int:
    for path in FILES:
        patch_file(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
