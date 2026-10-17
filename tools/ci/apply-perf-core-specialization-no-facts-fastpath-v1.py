#!/usr/bin/env python3
"""Temporary Core one-factor speed probe: avoid scanning all functions when
a direct call has no actionable integer, pointer-symbolic or Boolean-range fact.

Each selector's old first-match rule updates best only when fact_count>best.
With ZERO eligible actual facts, every compatible candidate necessarily has
fact_count=0, hence no candidate can be preferred over current_callee.
A return at that point is equivalent to the entire original linear scan.
No persistent indexes, OOM/lifetime, ABI or production source changes.
"""
from pathlib import Path
p=Path("src/core/core_lower.c")
src=p.read_text()
ids=(
    ("integer","core_select_transitive_integer_specialization(",
        "        known[parameter_index] = true;\n",
        "        known[parameter_index] = true;\n        has_relevant_fact = true;\n"),
    ("symbolic","core_select_transitive_symbolic_specialization(",
        "            &actual_symbol[i],\n            0U);\n",
        "            &actual_symbol[i],\n            0U);\n        if (actual_known[i]) has_relevant_fact = true;\n"),
    ("boolean","core_select_boolean_range_specialization(",
        "                context, minic_c0_call_argument(expression, i), 0U);\n",
        "                context, minic_c0_call_argument(expression, i), 0U);\n        if (actual_range[i]) has_relevant_fact = true;\n"),
)
for kind,fn,needle,addition in ids:
    start=src.index("static const MinicFunction *"+fn)
    end=src.index("\nstatic ",start+12)
    block=src[start:end]
    def once(old,new,where):
        global block
        if block.count(old)!=1:
            raise SystemExit("MINIC_CORE_NOFACTS_PATCH=FAIL "+kind+"/"+where)
        block=block.replace(old,new,1)
    once("    size_t candidate_index;\n",
         "    size_t candidate_index;\n    bool has_relevant_fact = false;\n","decl")
    once(needle,addition,"facts")
    once("    best = current_callee;\n",
         "    /* Every winning candidate must match >=1 known fact. When there\n"
         "     * are none, candidate_count>best_count can never hold. */\n"
         "    if (!has_relevant_fact) return current_callee;\n"
         "    best = current_callee;\n","skip")
    src=src[:start]+block+src[end:]
p.write_text(src)
print("MINIC_CORE_NOFACTS_FASTPATH=APPLIED selectors=3 temporary_only=true")
