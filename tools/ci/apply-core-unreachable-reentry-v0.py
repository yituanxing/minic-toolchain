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

# Normalized pointer-to-integer casts can be represented as BITCAST as well as
# CAST/CONVERSION. M181 already proves the pointer bits; admit the missing
# outer spelling so poisoned-pointer BUILD_BUG checks fold identically.
bitcast = Path("tools/ci/apply-pointer-bitcast-integer-cfg-v0.py")
exec(compile(bitcast.read_text(), str(bitcast), "exec"))

# Feature-disabled Linux code also uses non-inline internal helpers whose body
# is exactly one constant return.  Reuse the existing strict CFG-only helper
# evaluator for those functions as well; argument purity and single-return
# requirements remain unchanged.
internal_call = Path("tools/ci/apply-constant-internal-call-cfg-v0.py")
exec(compile(internal_call.read_text(), str(internal_call), "exec"))

# Evaluate CFG-only constant helpers in a temporary callee fact environment so
# constant call arguments become parameter-local facts. This lets the existing
# arithmetic/boolean closure prove helpers such as is_power_of_2(8) without
# changing runtime call lowering. Recursion remains depth-limited and fail-closed.
parameter_facts = Path("tools/ci/apply-constant-call-parameter-facts-v0.py")
exec(compile(parameter_facts.read_text(), str(parameter_facts), "exec"))

# File-scope internal const integer objects are immutable under defined C
# behavior. Expose their scalar initializer to the same CFG-only evaluator;
# this covers guard metadata emitted as static const bool without changing
# ordinary global-object lowering.
static_const = Path("tools/ci/apply-static-const-global-cfg-v0.py")
exec(compile(static_const.read_text(), str(static_const), "exec"))

# A CONFIG-disabled helper often begins with a constant if-return and then has
# runtime-dependent fallback code. Follow only a proven one-return selected arm;
# otherwise retain the existing fail-closed direct-return behavior.
leading_if = Path("tools/ci/apply-constant-call-leading-if-v0.py")
exec(compile(leading_if.read_text(), str(leading_if), "exec"))

# GNU statement expressions used by min/max-style kernel macros often contain
# only local constant initializers and one final pure expression. Interpret
# exactly that straight-line subset for CFG-only constant folding; any other
# statement shape remains fail-closed.
statement_expr = Path("tools/ci/apply-statement-expression-constant-cfg-v0.py")
exec(compile(statement_expr.read_text(), str(statement_expr), "exec"))

# The frontend may append a fallback return after a source-level unconditional
# `return NULL;`.  Null-pointer helper propagation must follow the first
# top-level return exactly as the integer helper closure already does.
null_first_return = Path("tools/ci/apply-null-helper-first-return-v0.py")
exec(compile(null_first_return.read_text(), str(null_first_return), "exec"))

# CONFIG-off headers also expose empty internal inline void helpers.  When every
# argument is proven pure, elide the no-op call before argument materialization
# so unused global addresses do not survive as link-time dependencies.
empty_void_call = Path("tools/ci/apply-empty-internal-void-call-elision-v0.py")
exec(compile(empty_void_call.read_text(), str(empty_void_call), "exec"))

# Tail closure consumes facts established by M177/M181 plus integer/symbolic
# specialization, so it must run after both residual constant closure and the
# complete focused semantic stack.
tail = Path("tools/ci/apply-tail-cfg-closure-v0.py")
exec(compile(tail.read_text(), str(tail), "exec"))
