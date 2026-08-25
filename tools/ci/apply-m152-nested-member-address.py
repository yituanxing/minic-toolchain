#!/usr/bin/env python3
from pathlib import Path

marker = "M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER"
lower_path = Path("src/core/core_lower.c")
ir_path = Path("src/core/core_ir.c")
codegen_path = Path("src/target/riscv64/core_codegen.c")
lower = lower_path.read_text()
ir = ir_path.read_text()
codegen = codegen_path.read_text()

if marker in lower:
    print("M155 extern void symbol address owner already staged")
    raise SystemExit(0)
if "M154_SIGNED_BIT_FIELD_UPDATE_OWNER" not in lower:
    raise SystemExit("M155 requires productized M154")

old_gate = """        if (!core_global_addressable_type(global->type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n"""
new_gate = r'''        /* M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER: GNU C permits linker-defined
           declaration-only `extern void` symbols such as __start_notes.  They
           have an address but no C object value to load/store.  Keep ordinary
           object addressability unchanged and admit only an extern, non-
           tentative, initializer-free void declaration at this source boundary. */
        if (!core_global_addressable_type(global->type) &&
            !(minic_type_is_void(global->type) && global->is_extern &&
              !global->is_tentative && global->initializer_count == 0U &&
              global->relocation_count == 0U && global->union_selection_count == 0U)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
if lower.count(old_gate) != 1:
    raise SystemExit("M155 could not locate Core global address gate")
lower = lower.replace(old_gate, new_gate, 1)

old_ir = """static bool core_global_addressable_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||\n           minic_type_is_array(type) || minic_type_is_record(type);\n}\n"""
new_ir = r'''/* M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER: a Core global entry names a symbol,
   not allocated private storage.  A void-typed linker symbol may therefore
   participate in GLOBAL_ADDRESS while LOAD/STORE still reject void pointees. */
static bool core_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_array(type) || minic_type_is_record(type) ||
           minic_type_is_void(type);
}
'''
if ir.count(old_ir) != 1:
    raise SystemExit("M155 could not locate Core IR global type owner")
ir = ir.replace(old_ir, new_ir, 1)

old_codegen = """/* M74_GLOBAL_RECORD_ADDRESS: RV64 global.addr lowers to `la symbol`;\n   the pointee's aggregate shape is irrelevant until field/element addressing. */\nstatic bool core_global_addressable_type(MinicType type) {\n    return core_scalar_type(type) || minic_type_is_array(type) ||\n           minic_type_is_record(type);\n}\n"""
new_codegen = r'''/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr lowers to `la symbol`; the pointee's storage shape is
   irrelevant until a later memory operation.  A declaration-only void symbol
   therefore needs no new load/store ABI support. */
static bool core_global_addressable_type(MinicType type) {
    return core_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type) || minic_type_is_void(type);
}
'''
if codegen.count(old_codegen) != 1:
    raise SystemExit("M155 could not locate RV64 global type owner")
codegen = codegen.replace(old_codegen, new_codegen, 1)

lower_path.write_text(lower)
ir_path.write_text(ir)
codegen_path.write_text(codegen)

Path("tests/compiler/c0/m155_extern_void_symbol_address.c").write_text(r'''#define __weak __attribute__((weak))
extern const void __start_notes __weak;
extern const void __stop_notes __weak;
extern void *memcpy(void *dst, const void *src, unsigned long n);

unsigned long notes_size(void) {
    return (unsigned long)(&__stop_notes - &__start_notes);
}

void notes_copy(char *buf, long off, unsigned long count) {
    memcpy(buf, &__start_notes + off, count);
}
''')
print("staged M155 extern void symbol address owner")
