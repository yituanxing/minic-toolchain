#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M178_UNREACHABLE_EXTERNAL_REENTRY"
if marker in text:
    print("MINIC_CORE_UNREACHABLE_REENTRY_V0=ALREADY")
    raise SystemExit(0)

old = '''            if (statement->kind != MINIC_STATEMENT_LABEL &&
                statement->kind != MINIC_STATEMENT_CASE &&
                statement->kind != MINIC_STATEMENT_DEFAULT) {
                if (core_unreachable_statement_has_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                continue;
            }
'''
new = '''            if (statement->kind != MINIC_STATEMENT_LABEL &&
                statement->kind != MINIC_STATEMENT_CASE &&
                statement->kind != MINIC_STATEMENT_DEFAULT) {
                if (core_unreachable_statement_has_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID)) {
                    MinicCoreBlockId detached_preheader;

                    /* M178_UNREACHABLE_EXTERNAL_REENTRY: an outer path may have
                       terminated before a structured statement while a goto,
                       asm-goto, or address-taken user label still enters that
                       statement from elsewhere in the function. Build the
                       structured subtree from an orphan preheader so its
                       internal CFG exists without inventing fallthrough from
                       the already-terminated path. */
                    if (!minic_core_function_add_block(
                            context->function, &detached_preheader)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    context->block_id = detached_preheader;
                    block_terminated = false;
                } else {
                    continue;
                }
            }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one unreachable external-reentry anchor, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_CORE_UNREACHABLE_REENTRY_V0=APPLIED")
