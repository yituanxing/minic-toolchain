#!/usr/bin/env python3
"""Retire the obsolete flattened do-while(0) Core lowering shortcut."""

from pathlib import Path


PATH = Path("src/core/core_lower.c")
text = PATH.read_text()

helper_begin = text.find("static bool normalized_do_while_zero_body(")
helper_end_marker = "/* M78_OMITTED_FOR_CONDITION:"
helper_end = text.find(helper_end_marker, helper_begin)
if helper_begin < 0 or helper_end < 0:
    raise SystemExit("M119 helper anchors not found")
if text.count("static bool normalized_do_while_zero_body(") != 1:
    raise SystemExit("M119 expected exactly one do-while-zero helper")
text = text[:helper_begin] + text[helper_end:]

old = '''    {
        MinicBlock single_iteration_body;

        if (normalized_do_while_zero_body(
                context, statement, body_source, &single_iteration_body)) {
            status = lower_block(context, &single_iteration_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            *terminated = body_terminated;
            return MINIC_CORE_LOWER_OK;
        }
    }

    iteration_source = body_source;
'''
new = '''    /* M119_GENERAL_DO_WHILE_ZERO_CFG: do not flatten normalized do-while(0)
       bodies ahead of the ordinary loop CFG. The old shortcut lowered the body
       before creating the loop exit block and therefore left legitimate break
       statements with MINIC_CORE_BLOCK_INVALID as their target. The standard
       lower_while path already owns condition/body/exit blocks, break routing,
       continue binding and normalized-for tails, so keep one control-flow owner. */
    iteration_source = body_source;
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"M119 shortcut anchor: expected one match, got {count}")
text = text.replace(old, new, 1)

if "normalized_do_while_zero_body" in text:
    raise SystemExit("M119 stale do-while-zero helper reference remains")
PATH.write_text(text)
print("M119 retired flattened do-while-zero shortcut")
