#!/usr/bin/env bash
# Direct host contract for transient AST CALL selection; source FunctionBody
# and canonical CALL.function_id are immutable across all specialization passes.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/tests/compiler/c0/p43_inline_call_plan_identity.c" \
    "$root/src/compiler/inline_specialize_common.c" \
    -o "$work/p43-inline-call-plan-identity"
"$work/p43-inline-call-plan-identity"
