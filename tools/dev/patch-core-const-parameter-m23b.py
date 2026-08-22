#!/usr/bin/env python3
from pathlib import Path

lower_path = Path('src/core/core_lower.c')
text = lower_path.read_text()
begin_marker = 'static MinicCoreLowerStatus lower_parameter_ingress(MinicCoreLowerContext *context) {'
end_marker = 'static MinicCoreLowerStatus append_field_address('
if text.count(begin_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit('M23b parameter-ingress function boundary mismatch')
begin = text.index(begin_marker)
end = text.index(end_marker, begin)
body = text[begin:end]

old_decl = '        MinicLocalId local_id;\n        MinicType pointer_type;\n'
new_decl = '        MinicLocalId local_id;\n        MinicType parameter_value_type;\n        MinicType pointer_type;\n'
if body.count(old_decl) != 1:
    raise SystemExit(f'M23b parameter declaration anchor count={body.count(old_decl)}')
body = body.replace(old_decl, new_decl, 1)

old_guard = '''        if (!core_memory_scalar_type(parameter->type) || minic_type_is_const(parameter->type) ||
            minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
new_guard = '''        if (!core_memory_scalar_type(parameter->type) || minic_type_is_volatile(parameter->type) ||
            parameter->is_array || parameter->is_register_storage) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
if body.count(old_guard) != 1:
    raise SystemExit(f'M23b parameter guard anchor count={body.count(old_guard)}')
body = body.replace(old_guard, new_guard, 1)

old_match = '''        if (!minic_type_equal(parameter->type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
new_match = '''        if (!minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !core_memory_scalar_type(parameter_value_type) ||
            minic_type_is_const(parameter_value_type) ||
            minic_type_is_volatile(parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
if body.count(old_match) != 1:
    raise SystemExit(f'M23b parameter type anchor count={body.count(old_match)}')
body = body.replace(old_match, new_match, 1)

old_type = '        instruction.type = parameter->type;\n'
new_type = '        instruction.type = parameter_value_type;\n'
if body.count(old_type) != 1:
    raise SystemExit(f'M23b parameter instruction type anchor count={body.count(old_type)}')
body = body.replace(old_type, new_type, 1)
text = text[:begin] + body + text[end:]
lower_path.write_text(text)

shadow_path = Path('tests/core/run-core-ir-shadow.sh')
shadow = shadow_path.read_text()
old_shadow = '''cat >"$work_dir/qualified-parameter.i" <<'EOF'
unsigned long qualified_parameter(const unsigned long value) {
    return value;
}
EOF

"$MINIC" -S "$work_dir/qualified-parameter.i" -o "$work_dir/qualified-parameter-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/qualified-parameter.i"     -o "$work_dir/qualified-parameter-shadow.s"
cmp "$work_dir/qualified-parameter-normal.s" "$work_dir/qualified-parameter-shadow.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/qualified-parameter.i"     -o "$work_dir/qualified-parameter-strict.s" 2>"$work_dir/qualified-parameter-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted a qualified parameter" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'qualified_parameter'"     "$work_dir/qualified-parameter-strict.err" >/dev/null
'''
new_shadow = '''cat >"$work_dir/qualified-parameter.i" <<'EOF'
unsigned long qualified_parameter(const unsigned long value) {
    return value;
}
EOF
check_strict_case qualified-parameter

cat >"$work_dir/pointee-const-parameter.i" <<'EOF'
int pointee_const_parameter(const int *value) {
    return *value;
}
EOF
check_strict_case pointee-const-parameter

cat >"$work_dir/volatile-parameter-unsupported.i" <<'EOF'
unsigned long volatile_parameter(volatile unsigned long value) {
    return value;
}
EOF
"$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \\
    -o "$work_dir/volatile-parameter-unsupported-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \\
    -o "$work_dir/volatile-parameter-unsupported-shadow.s"
cmp "$work_dir/volatile-parameter-unsupported-normal.s" \\
    "$work_dir/volatile-parameter-unsupported-shadow.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \\
    -o "$work_dir/volatile-parameter-unsupported-strict.s" \\
    2>"$work_dir/volatile-parameter-unsupported-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted a volatile parameter" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'volatile_parameter'" \\
    "$work_dir/volatile-parameter-unsupported-strict.err" >/dev/null
'''
if shadow.count(old_shadow) != 1:
    raise SystemExit(f'M23b qualified-parameter shadow anchor count={shadow.count(old_shadow)}')
shadow = shadow.replace(old_shadow, new_shadow, 1)
shadow_path.write_text(shadow)

Path('tests/compiler/c0/core_const_parameter_m23b.c').write_text(r'''unsigned long core_m23b_const_parameter(const unsigned long value) {
    return value + 1UL;
}

unsigned long core_m23b_const_pointer_parameter(int *const pointer) {
    return (unsigned long)(*pointer);
}
''')
Path('tests/compiler/c0/core_const_parameter_m23b_runtime.c').write_text(r'''extern unsigned long core_m23b_const_parameter(unsigned long);
extern unsigned long core_m23b_const_pointer_parameter(int *);

int main(void) {
    int value = 37;
    if (core_m23b_const_parameter(41UL) != 42UL) return 1;
    if (core_m23b_const_pointer_parameter(&value) != 37UL) return 2;
    return 0;
}
''')
Path('tests/compiler/c0/run-core-const-parameter-m23b.sh').write_text(r'''#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-const-parameter-m23b}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_const_parameter_m23b.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_const_parameter_m23b_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_const_parameter_m23b_runtime.c tests/compiler/c0/core_const_parameter_m23b.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-const-parameter-m23b'
''')
print('staged M23b top-level const scalar parameter ingress')
