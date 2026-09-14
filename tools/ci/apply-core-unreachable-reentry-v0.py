#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M178_UNREACHABLE_EXTERNAL_REENTRY"
if marker in text:
    print("MINIC_CORE_UNREACHABLE_REENTRY_V0=ALREADY")
else:
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
                       statement from elsewhere in the function.  Build the
                       structured subtree from an orphan preheader: this keeps
                       its internal CFG available without inventing fallthrough
                       from the already-terminated path.  User-label Core blocks
                       remain the real externally rooted entry points.  This is
                       the generic form of the established unreachable-while
                       detached-preheader handling above. */
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

# Residual constant/CFG closure deliberately runs after the complete focused
# semantic stack, because it reuses core_cfg_pure_call_argument from the
# constant-inline pass and validates several remaining Linux BUILD_BUG forms.
closure = Path("tools/ci/apply-residual-constant-closure-v0.py")
exec(compile(closure.read_text(), str(closure), "exec"))
