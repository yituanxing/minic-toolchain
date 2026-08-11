#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, content):
    (ROOT / path).write_text(content)


def one(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return content.replace(old, new, 1)


p = "src/frontend/attribute.h"
s = read(p)
s = one(
    s,
    '''    MINIC_ATTRIBUTE_ALWAYS_INLINE,\n    MINIC_ATTRIBUTE_COLD,\n''',
    '''    MINIC_ATTRIBUTE_ALWAYS_INLINE,\n    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,\n    MINIC_ATTRIBUTE_COLD,\n''',
    "attribute kind",
)
write(p, s)

p = "src/frontend/attribute.c"
s = read(p)
s = one(
    s,
    '''    MINIC_ATTRIBUTE_ENTRY("__always_inline__",\n                          MINIC_ATTRIBUTE_ALWAYS_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n''',
    '''    MINIC_ATTRIBUTE_ENTRY("__always_inline__",\n                          MINIC_ATTRIBUTE_ALWAYS_INLINE,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION),\n    MINIC_ATTRIBUTE_ENTRY("externally_visible",\n                          MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n    MINIC_ATTRIBUTE_ENTRY("__externally_visible__",\n                          MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,\n                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),\n''',
    "externally visible descriptors",
)
write(p, s)

p = "tests/compiler/c0/gnu_prefix_function_attributes.c"
s = read(p)
s += r'''

/* Linux signal/start_kernel shapes: externally_visible preserves public reachability
 * under whole-program optimization. MiniC never internalizes public symbols, so the
 * bounded semantic effect is intentionally parse-only while external linkage stays. */
__attribute__((__externally_visible__))
void externally_visible_decl(int value);

__attribute__((__externally_visible__)) __attribute__((__cold__))
__attribute__((__section__(".probe.externally-visible.text")))
void externally_visible_decl(int value)
{
    (void)value;
}

extern __attribute__((__externally_visible__)) int externally_visible_object;

int *externally_visible_object_address(void)
{
    return &externally_visible_object;
}
'''
write(p, s)

p = "tests/compiler/c0/run-gnu-prefix-function-attributes.sh"
s = read(p)
s = one(
    s,
    '''grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument gnu-inline=static-only'\n''',
    '''grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null\ngrep -F 'externally_visible_decl:' "$assembly" >/dev/null\ngrep -F '.globl externally_visible_decl' "$assembly" >/dev/null\ngrep -F '.section .probe.externally-visible.text' "$assembly" >/dev/null\ngrep -F 'externally_visible_object' "$assembly" >/dev/null\nif grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then\n    printf '%s\\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2\n    exit 1\nfi\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static-only'\n''',
    "externally visible runner",
)
write(p, s)
