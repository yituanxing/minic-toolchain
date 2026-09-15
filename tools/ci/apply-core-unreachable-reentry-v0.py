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

for script in (
    "apply-residual-constant-closure-v0.py",
    "apply-pointer-bitcast-integer-cfg-v0.py",
    "apply-constant-internal-call-cfg-v0.py",
    "apply-constant-call-parameter-facts-v0.py",
    "apply-static-const-global-cfg-v0.py",
    "apply-constant-call-leading-if-v0.py",
    "apply-statement-expression-constant-cfg-v0.py",
    "apply-null-helper-first-return-v0.py",
    "apply-empty-internal-void-call-elision-v0.py",
    "apply-constant-if-external-reentry-v0.py",
    "apply-pure-dereference-cfg-v0.py",
    "apply-cfg-annihilator-priority-v0.py",
    "apply-statement-expression-postlower-fact-v0.py",
    "apply-loop-invariant-local-facts-v0.py",
    "apply-tail-cfg-closure-v0.py",
):
    path = Path("tools/ci") / script
    exec(compile(path.read_text(), str(path), "exec"))
