#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/attribute.h",
    '''    MINIC_ATTRIBUTE_MALLOC,\n    MINIC_ATTRIBUTE_ALLOC_SIZE,\n    MINIC_ATTRIBUTE_UNUSED,\n''',
    '''    MINIC_ATTRIBUTE_MALLOC,\n    MINIC_ATTRIBUTE_ALLOC_SIZE,\n    MINIC_ATTRIBUTE_ASSUME_ALIGNED,\n    MINIC_ATTRIBUTE_UNUSED,\n''',
    "assume_aligned attribute kind",
)

replace_once(
    "src/frontend/attribute.c",
    '''    {\n        "__alloc_size__",\n        sizeof("__alloc_size__") - 1U,\n        MINIC_ATTRIBUTE_ALLOC_SIZE,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        2U,\n        true,\n    },\n    MINIC_ATTRIBUTE_ENTRY("__unused__",\n''',
    '''    {\n        "__alloc_size__",\n        sizeof("__alloc_size__") - 1U,\n        MINIC_ATTRIBUTE_ALLOC_SIZE,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        2U,\n        true,\n    },\n    {\n        "assume_aligned",\n        sizeof("assume_aligned") - 1U,\n        MINIC_ATTRIBUTE_ASSUME_ALIGNED,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        2U,\n        true,\n    },\n    {\n        "__assume_aligned__",\n        sizeof("__assume_aligned__") - 1U,\n        MINIC_ATTRIBUTE_ASSUME_ALIGNED,\n        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,\n        MINIC_ATTRIBUTE_TARGET_FUNCTION,\n        1U,\n        2U,\n        true,\n    },\n    MINIC_ATTRIBUTE_ENTRY("__unused__",\n''',
    "assume_aligned descriptors",
)

replace_once(
    "tests/compiler/c0/gnu_function_attributes.c",
    '''extern void *allocate_matrix(unsigned long rows, unsigned long columns)\n    __attribute__((__alloc_size__(1, 2)));\n\nextern int stable_transform(int value) __attribute__((const));\n''',
    '''extern void *allocate_matrix(unsigned long rows, unsigned long columns)\n    __attribute__((__alloc_size__(1, 2)));\n\nextern void *allocate_aligned(unsigned long count)\n    __attribute__((__assume_aligned__((8)))) __attribute__((__malloc__));\n\nextern void *allocate_aligned_offset(unsigned long count)\n    __attribute__((assume_aligned(__alignof__(unsigned long long), 4)));\n\nextern int stable_transform(int value) __attribute__((const));\n''',
    "positive assume_aligned declarations",
)

replace_once(
    "tests/compiler/c0/gnu_function_attributes.c",
    '''    void *matrix = allocate_matrix(2, 2);\n    memory_copy(destination, source, 4);\n    return allocated != (void *)0 && sized != (void *)0 && matrix != (void *)0 &&\n           memory_compare(destination, source, 4) && stable_transform(1);\n''',
    '''    void *matrix = allocate_matrix(2, 2);\n    void *aligned = allocate_aligned(8);\n    void *aligned_offset = allocate_aligned_offset(8);\n    memory_copy(destination, source, 4);\n    return allocated != (void *)0 && sized != (void *)0 && matrix != (void *)0 &&\n           aligned != (void *)0 && aligned_offset != (void *)0 &&\n           memory_compare(destination, source, 4) && stable_transform(1);\n''',
    "exercise assume_aligned declarations",
)

replace_once(
    "tests/compiler/c0/run-gnu-function-attributes.sh",
    '''grep -F '  call allocate_matrix' "$work/gnu_function_attributes.s" >/dev/null\ngrep -F '  call memory_copy' "$work/gnu_function_attributes.s" >/dev/null\n''',
    '''grep -F '  call allocate_matrix' "$work/gnu_function_attributes.s" >/dev/null\ngrep -F '  call allocate_aligned' "$work/gnu_function_attributes.s" >/dev/null\ngrep -F '  call allocate_aligned_offset' "$work/gnu_function_attributes.s" >/dev/null\ngrep -F '  call memory_copy' "$work/gnu_function_attributes.s" >/dev/null\n''',
    "verify assume_aligned calls",
)

replace_once(
    "tests/compiler/c0/run-gnu-function-attributes.sh",
    '''done\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,alloc-size,noreturn,deprecated,const-keyword arguments=registry-validated placement=pre-declarator,suffix unknown=reject aligned=not-silently-ignored'\n''',
    '''done\n\nfor mode in missing too-many; do\n    cat >"$work/assume-aligned-$mode.c" <<EOF\nextern void *bad(void) __attribute__((__assume_aligned__($([ "$mode" = too-many ] && printf '8, 0, 1'))));\nEOF\n    "$host_cc" -E -P -x c "$work/assume-aligned-$mode.c" -o "$work/assume-aligned-$mode.i"\n    set +e\n    "$minic" -S "$work/assume-aligned-$mode.i" -o "$work/assume-aligned-$mode.s" \\\n        >"$work/assume-aligned-$mode.stdout" 2>"$work/assume-aligned-$mode.stderr"\n    status=$?\n    set -e\n    test "$status" -ne 0\n    grep -F 'GNU attribute has an invalid number of arguments' \\\n        "$work/assume-aligned-$mode.stderr" >/dev/null\ndone\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,alloc-size,assume-aligned,noreturn,deprecated,const-keyword arguments=registry-validated placement=pre-declarator,suffix optimization-metadata=parse-only unknown=reject aligned=not-silently-ignored'\n''',
    "assume_aligned arity regressions",
)
