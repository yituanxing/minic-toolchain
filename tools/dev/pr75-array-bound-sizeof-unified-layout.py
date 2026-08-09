#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
start = text.find("static bool array_bound_type_size(")
end = text.find("\nstatic bool parse_array_bound_sizeof(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate array_bound_type_size")
replacement = r'''static bool constant_type_layout(const MinicC0Program *program,
                                 MinicType type,
                                 unsigned int depth,
                                 uint64_t *size,
                                 uint64_t *alignment);

static bool array_bound_type_size(const MinicC0Program *program, MinicType type, uint64_t *size) {
    uint64_t alignment;

    return constant_type_layout(program, type, 0U, size, &alignment);
}
'''
path.write_text(text[:start] + replacement + text[end:])
print("staged sizeof(type) through unified RV64 constant layout")
