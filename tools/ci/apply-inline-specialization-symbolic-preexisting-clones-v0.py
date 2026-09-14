#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

# Symbolic specialization runs after integer specialization.  The transitive
# pass must revisit those already-created integer clones: their closed integer
# parameter facts can turn expressions such as &global[a][b] into symbolic
# addresses for an always-inline callee.  Restricting the walk to functions
# created during the symbolic pass misses exactly that case.
old_decl = "    size_t original_function_count;\n"
old_assign = "    original_function_count = program->function_count;\n\n"
old_loop = "        for (caller_index = original_function_count;\n"
new_loop = "        for (caller_index = 0U;\n"

for label, old in (("declaration", old_decl), ("assignment", old_assign), ("loop", old_loop)):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one symbolic preexisting-clone {label} anchor, found {count}")

text = text.replace(old_decl, "", 1)
text = text.replace(old_assign, "", 1)
text = text.replace(old_loop, new_loop, 1)
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_PREEXISTING_CLONES_V0=APPLIED")
