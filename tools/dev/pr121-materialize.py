from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/attribute.h",
    "    MINIC_ATTRIBUTE_NOINLINE,\n    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,\n",
    "    MINIC_ATTRIBUTE_NOINLINE,\n    MINIC_ATTRIBUTE_NOCLONE,\n    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,\n",
)

replace_once(
    "src/frontend/attribute.c",
    '''    {
        "__noinline__",
        sizeof("__noinline__") - 1U,
        MINIC_ATTRIBUTE_NOINLINE,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
''',
    '''    {
        "__noinline__",
        sizeof("__noinline__") - 1U,
        MINIC_ATTRIBUTE_NOINLINE,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
    {
        "noclone",
        sizeof("noclone") - 1U,
        MINIC_ATTRIBUTE_NOCLONE,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
    {
        "__noclone__",
        sizeof("__noclone__") - 1U,
        MINIC_ATTRIBUTE_NOCLONE,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
''',
)

fixture = Path("tests/compiler/c0/deferred_declarator_attributes.c")
text = fixture.read_text()
addition = r'''

void __attribute__((__noinline__)) __attribute__((__noclone__))
    kfree_skb_reason_shape(int reason);

void kfree_skb_reason_shape(int reason) {
    (void)reason;
}

void __attribute__((noclone)) *noclone_after_return_pointer(int value);

void *noclone_after_return_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}
'''
if "kfree_skb_reason_shape" in text:
    raise SystemExit("noclone fixture already materialized")
fixture.write_text(text + addition)

Path("tests/compiler/c0/invalid_noclone_argument.c").write_text(r'''void __attribute__((noclone(1))) bad_noclone_argument(void);
''')
Path("tests/compiler/c0/invalid_noclone_object.c").write_text(r'''extern int __attribute__((__noclone__)) noclone_object;
''')

runner = Path("tests/compiler/c0/run-deferred-declarator-attributes.sh")
text = runner.read_text()
needle = '''grep -F 'call map_after_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
'''
addition = needle + '''grep -F 'kfree_skb_reason_shape:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'noclone_after_return_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
'''
if text.count(needle) != 1:
    raise SystemExit(f"deferred attr positive anchor mismatch: {text.count(needle)}")
text = text.replace(needle, addition, 1)

pass_anchor = "printf '%s\\n' 'PASS compiler/c0/deferred_declarator_attributes"
negative = r'''if "$minic" -S "$root/tests/compiler/c0/invalid_noclone_argument.c" \
    -o "$work/invalid-noclone-argument.s" >"$work/invalid-noclone-argument.stdout" 2>"$work/invalid-noclone-argument.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: noclone accepted an argument' >&2
    exit 1
fi
grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid-noclone-argument.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_noclone_object.c" \
    -o "$work/invalid-noclone-object.s" >"$work/invalid-noclone-object.stdout" 2>"$work/invalid-noclone-object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: noclone leaked onto object' >&2
    exit 1
fi
grep -F 'unsupported GNU object attribute' "$work/invalid-noclone-object.stderr" >/dev/null

'''
pos = text.find(pass_anchor)
if pos < 0:
    raise SystemExit("deferred attr PASS anchor missing")
text = text[:pos] + negative + text[pos:]
text = text.replace(
    "section=preserved noinline=parse-only",
    "section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg",
    1,
)
runner.write_text(text)
