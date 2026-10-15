#!/usr/bin/env python3
"""Opt-in AST dynamic array memory experiment: 1.5x instead of 2x growth.

ONLY expressions/functions (680B / 6536B real Linux AST entries).
Keep plain array-of-values layout, stable IDs, all semantic consumers and
other arrays untouched. Allocation failure still returns false.
"""
from pathlib import Path

p=Path("src/frontend/ast.c")
source=p.read_text()
anchor='''static char *minic_copy_name(const char *name, size_t name_length) {'''
if source.count(anchor)!=1:
    raise SystemExit("AST_GROWTH expected one helper insertion boundary")
helper='''static bool minic_perf_grow_large_ast(void **data, size_t *capacity,
                                      size_t count, size_t element_size) {
    size_t grown;
    void *resized;

    if (count < *capacity)
        return true;
    if (element_size == 0U || count == SIZE_MAX)
        return false;
    if (*capacity == 0U) {
        grown = 16U;
    } else {
        size_t extra = *capacity / 2U;
        if (extra == 0U)
            extra = 1U;
        if (*capacity > SIZE_MAX - extra)
            return false;
        grown = *capacity + extra;
    }
    if (grown <= count)
        grown = count + 1U;
    if (grown > SIZE_MAX / element_size)
        return false;
    resized = realloc(*data, grown * element_size);
    if (resized == NULL)
        return false;
    *data = resized;
    *capacity = grown;
    return true;
}

'''
source=source.replace(anchor,helper+anchor,1)
for array_name in ("expressions","functions"):
    old='if (!minic_grow_array((void **)&program->'+array_name+','
    new='if (!minic_perf_grow_large_ast((void **)&program->'+array_name+','
    if source.count(old)!=1:
        raise SystemExit(f"AST_GROWTH expected single {array_name} append")
    source=source.replace(old,new,1)
p.write_text(source)
print("MINIC_PERF_AST_1P5_GROWTH_V1=APPLIED arrays=expressions,functions")
