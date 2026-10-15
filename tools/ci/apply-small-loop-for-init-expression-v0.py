#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M195_SMALL_LOOP_FOR_INIT_EXPRESSION"
if marker in text:
    print("MINIC_SMALL_LOOP_FOR_INIT_EXPRESSION_V0=ALREADY")
else:
    fn = "static bool core_cfg_small_constant_loop_call("
    start = text.find(fn)
    if start < 0:
        raise SystemExit("small-loop for-init: evaluator missing")
    end = text.find("\nstatic bool core_cfg_constant_inline_call(", start)
    if end < 0:
        raise SystemExit("small-loop for-init: evaluator end missing")
    region = text[start:end]
    old = '''        if (statement->kind == MINIC_STATEMENT_ASSIGN) {
            if (!core_cfg_small_loop_execute_statement(&callee_context, statement)) {
                free(facts);
                return false;
            }
            continue;
        }
'''
    new = '''        /* M195_SMALL_LOOP_FOR_INIT_EXPRESSION: parse_for() normalizes
           `for (i = 0; ...; i++)` by emitting the initializer as a top-level
           EXPRESSION statement immediately before the WHILE.  It is the same
           strict local-only assignment subset already accepted by the helper;
           admit it only before the one loop has been seen. */
        if (statement->kind == MINIC_STATEMENT_ASSIGN ||
            (!saw_loop && statement->kind == MINIC_STATEMENT_EXPRESSION)) {
            if (!core_cfg_small_loop_execute_statement(&callee_context, statement)) {
                free(facts);
                return false;
            }
            continue;
        }
'''
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"small-loop for-init anchor: expected one, found {count}")
    region = region.replace(old, new, 1)
    text = text[:start] + region + text[end:]
    p.write_text(text)
    print("MINIC_SMALL_LOOP_FOR_INIT_EXPRESSION_V0=APPLIED")
