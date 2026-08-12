from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


old = '''static bool gnu_function_pointer_bridge_call_conversion_compatible(const MinicC0Program *program,\n                                                                   MinicType target,\n                                                                   const MinicExpression *source) {\n'''
new = '''static bool gnu_function_pointer_to_void_call_conversion_compatible(MinicType target,\n                                                                    MinicType source) {\n    MinicType source_pointee;\n    MinicType void_pointer;\n\n    return minic_type_pointer_to(minic_type_void(), &void_pointer) &&\n           minic_type_equal(target, void_pointer) &&\n           minic_type_pointee(source, &source_pointee) && minic_type_is_function(source_pointee);\n}\n\nstatic bool gnu_function_pointer_bridge_call_conversion_compatible(const MinicC0Program *program,\n                                                                   MinicType target,\n                                                                   const MinicExpression *source) {\n'''
replace_once("src/frontend/parser_expression.c", old, new)

old = '''        pointer_sign_call_conversion_compatible(target_type, source->type) ||\n        gnu_function_pointer_bridge_call_conversion_compatible(\n            parser->program, target_type, source);\n'''
new = '''        pointer_sign_call_conversion_compatible(target_type, source->type) ||\n        gnu_function_pointer_to_void_call_conversion_compatible(target_type, source->type) ||\n        gnu_function_pointer_bridge_call_conversion_compatible(\n            parser->program, target_type, source);\n'''
replace_once("src/frontend/parser_expression.c", old, new)

Path("tests/compiler/c0/gnu_function_pointer_to_void_call.c").write_text(r'''void *dereference_symbol_descriptor(void *ptr)
{
    return ptr;
}

void arch_rethook_trampoline(void)
{
}

unsigned long linux_shaped_direct(void)
{
    return (unsigned long)dereference_symbol_descriptor(arch_rethook_trampoline);
}

unsigned long through_function_pointer(void (*callback)(void))
{
    return (unsigned long)dereference_symbol_descriptor(callback);
}

int drive_function_pointer(void)
{
    return through_function_pointer(arch_rethook_trampoline) != 0UL;
}
''')

old = '''"$minic" -S "$work/bridge.i" -o "$work/bridge.s"\ngrep -F '  call __cpuhp_setup_state' "$work/bridge.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/gnu_function_pointer_bridge_call explicit-void-bridge=2 target-bitcast=1 direct-incompatible=still-strict'\n\nfor name in invalid_direct_incompatible_function_pointer_call \\\n'''
new = '''"$minic" -S "$work/bridge.i" -o "$work/bridge.s"\ngrep -F '  call __cpuhp_setup_state' "$work/bridge.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/gnu_function_pointer_bridge_call explicit-void-bridge=2 target-bitcast=1 direct-incompatible=still-strict'\n\n"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_pointer_to_void_call.c" \\\n    -o "$work/function-to-void.i"\n"$minic" -S "$work/function-to-void.i" -o "$work/function-to-void.s"\ntest "$(grep -c -F '  call dereference_symbol_descriptor' "$work/function-to-void.s")" -ge 2\ngrep -F '  call through_function_pointer' "$work/function-to-void.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/gnu_function_pointer_to_void_call direct-function=1 function-pointer-expression=1 target-void-pointer=1'\n\nfor name in invalid_direct_incompatible_function_pointer_call \\\n'''
replace_once("tests/compiler/c0/run-gnu-function-pointer-bridge-call.sh", old, new)
