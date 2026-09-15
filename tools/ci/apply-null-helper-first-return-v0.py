#!/usr/bin/env python3
from pathlib import Path

# CFG-only null-pointer helper recognition already exists in
# apply-local-null-pointer-facts-v0.py.  The frontend may append a synthetic
# fallback return after a source-level unconditional top-level return, so a
# strict statement_count == 1 test rejects helpers such as CONFIG-off
# pmd_trans_huge_lock() even though its first statement is `return NULL;`.
# Follow normal C reachability: once the first top-level statement is an
# unconditional return, later top-level statements are unreachable.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M185_NULL_HELPER_FIRST_RETURN"
if marker in text:
    print("MINIC_NULL_HELPER_FIRST_RETURN_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_expression_known_null_pointer_depth(\n"
start = text.find(fn)
if start < 0:
    raise SystemExit("null-pointer helper evaluator missing")
end = text.find("\nstatic bool core_expression_known_null_pointer(\n", start + len(fn))
if end < 0:
    raise SystemExit("null-pointer helper evaluator end missing")
body = text[start:end]
old = '''        if (callee_body == NULL || callee_body->statement_count != 1U) {
            return false;
        }
'''
new = '''        /* M185_NULL_HELPER_FIRST_RETURN: the first top-level unconditional
           return terminates the function; parser-added fallback returns after
           it are unreachable and must not defeat CFG-only null propagation. */
        if (callee_body == NULL || callee_body->statement_count == 0U) {
            return false;
        }
'''
count = body.count(old)
if count != 1:
    raise SystemExit(f"null-helper statement-count anchor: expected one, found {count}")
body = body.replace(old, new, 1)
text = text[:start] + body + text[end:]
p.write_text(text)
print("MINIC_NULL_HELPER_FIRST_RETURN_V0=APPLIED top_level_return=1")
