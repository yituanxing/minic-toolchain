#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
start = text.find("static bool minic_specialize_inline_symbolic_calls(\n")
end = text.find("    if (initial_clone_count != 0U || transitive_clone_count != 0U) {", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate symbolic specialization lookup body")
body = text[start:end]

decl = "    size_t original_function_count;\n"
if body.count(decl) != 1:
    raise SystemExit(f"symbolic specialization original-count decl={body.count(decl)}")
body = body.replace(
    decl,
    decl + "    size_t specialization_begin;\n",
    1,
)

init = "    original_function_count = program->function_count;\n\n"
if body.count(init) != 1:
    raise SystemExit(f"symbolic specialization original-count init={body.count(init)}")
body = body.replace(
    init,
    """    original_function_count = program->function_count;
    specialization_begin = original_function_count;
    for (expression_index = 0U; expression_index < original_function_count;
         ++expression_index) {
        if (program->functions[expression_index].is_integer_specialization) {
            specialization_begin = expression_index;
            break;
        }
    }

""",
    1,
)

needle = "for (candidate_index = 0U;"
count = body.count(needle)
if count != 2:
    raise SystemExit(f"symbolic specialization candidate loops={count}")
body = body.replace(
    needle,
    "for (candidate_index = specialization_begin;",
)

text = text[:start] + body + text[end:]
p.write_text(text)
print("MINIC_SYMBOLIC_SPECIALIZATION_CANDIDATE_TAIL_V0=APPLIED")
