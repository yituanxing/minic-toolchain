from pathlib import Path

# Attribute kind.
header = Path("src/frontend/attribute.h")
text = header.read_text()
old = "    MINIC_ATTRIBUTE_UNUSED,\n    MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,\n"
new = "    MINIC_ATTRIBUTE_UNUSED,\n    MINIC_ATTRIBUTE_USED,\n    MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,\n"
if text.count(old) != 1:
    raise SystemExit(f"attribute kind anchor mismatch: {text.count(old)}")
header.write_text(text.replace(old, new, 1))

# GCC `used` applies to functions and variables. MiniC currently emits every
# defined function/global object and has no dead-entity elimination, so the
# required emission behavior is already satisfied and belongs to the existing
# parse-only optimization class. Keep arity/targets explicit in the registry.
registry = Path("src/frontend/attribute.c")
text = registry.read_text()
old = '''    MINIC_ATTRIBUTE_ENTRY("__unused__",
                          MINIC_ATTRIBUTE_UNUSED,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__no_instrument_function__",
'''
new = '''    MINIC_ATTRIBUTE_ENTRY("__unused__",
                          MINIC_ATTRIBUTE_UNUSED,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    {
        "used",
        sizeof("used") - 1U,
        MINIC_ATTRIBUTE_USED,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT,
        0U,
        0U,
        true,
    },
    {
        "__used__",
        sizeof("__used__") - 1U,
        MINIC_ATTRIBUTE_USED,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT,
        0U,
        0U,
        true,
    },
    MINIC_ATTRIBUTE_ENTRY("__no_instrument_function__",
'''
if text.count(old) != 1:
    raise SystemExit(f"used descriptor anchor mismatch: {text.count(old)}")
registry.write_text(text.replace(old, new, 1))

# Extend the existing declarator-attribute fixture with both legal targets.
# Static-object section metadata is a separate semantic owner and is intentionally
# left for the next Linux-driven slice instead of being mixed into this registry PR.
fixture = Path("tests/compiler/c0/deferred_declarator_attributes.c")
text = fixture.read_text()
append = '''

static int __attribute__((__used__)) used_object_shape = 7;

void __attribute__((used)) used_function_shape(void);

void used_function_shape(void) {
    (void)used_object_shape;
}
'''
if "used_object_shape" in text or "used_function_shape" in text:
    raise SystemExit("used fixture already present")
fixture.write_text(text.rstrip() + append)

Path("tests/compiler/c0/invalid_used_argument.c").write_text(
    'static int __attribute__((used(1))) invalid_used_argument = 0;\n'
)
Path("tests/compiler/c0/invalid_used_field.c").write_text(
    'struct InvalidUsedField { int value __attribute__((used)); };\n'
)

runner = Path("tests/compiler/c0/run-deferred-declarator-attributes.sh")
text = runner.read_text()
old = '''grep -F 'noclone_after_return_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \\
'''
new = '''grep -F 'noclone_after_return_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'used_object_shape:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'used_function_shape:' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \\
'''
if text.count(old) != 1:
    raise SystemExit(f"runner positive anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''grep -F 'unsupported GNU object attribute' "$work/invalid-noclone-object.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg'
'''
new = '''grep -F 'unsupported GNU object attribute' "$work/invalid-noclone-object.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_used_argument.c" \\
    -o "$work/invalid-used-argument.s" >"$work/invalid-used-argument.stdout" 2>"$work/invalid-used-argument.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/deferred_declarator_attributes: used accepted an argument' >&2
    exit 1
fi
grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid-used-argument.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_used_field.c" \\
    -o "$work/invalid-used-field.s" >"$work/invalid-used-field.stdout" 2>"$work/invalid-used-field.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/deferred_declarator_attributes: used leaked onto record field' >&2
    exit 1
fi
grep -F 'unsupported GNU record field attribute' "$work/invalid-used-field.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg used=parse-only+function-object+zero-arg'
'''
if text.count(old) != 1:
    raise SystemExit(f"runner negative anchor mismatch: {text.count(old)}")
runner.write_text(text.replace(old, new, 1))
