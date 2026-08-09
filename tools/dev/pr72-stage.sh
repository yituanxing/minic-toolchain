#!/bin/sh
set -eu

for patch in \
    tools/dev/pr71-discovery.patch \
    tools/dev/pr71-record-copy.patch \
    tools/dev/pr71-local-array-identity.patch \
    tools/dev/pr71-do-while.patch \
    tools/dev/pr71-assignment-chain.patch \
    tools/dev/pr71-bitwise-platform.patch \
    tools/dev/pr71-float-conversion.patch \
    tools/dev/pr71-conditional-void.patch \
    tools/dev/pr71-fixed-double-abi.patch \
    tools/dev/pr72-enum-frontier.patch \
    tools/dev/pr72-union-frontier.patch \
    tools/dev/pr72-static-scalar.patch \
    tools/dev/pr72-integer-suffix-type.patch \
    tools/dev/pr72-global-scalar-read.patch \
    tools/dev/pr72-assignment-expression.patch \
    tools/dev/pr72-variadic-definition.patch \
    tools/dev/pr72-probe-stdarg.patch \
    tools/dev/pr72-probe-math.patch \
    tools/dev/pr72-probe-stdio.patch \
    tools/dev/pr72-void-object-pointer.patch \
    tools/dev/pr72-expression-arena-safety.patch \
    tools/dev/pr72-null-statement.patch \
    tools/dev/pr72-pointer-difference-qualification.patch \
    tools/dev/pr72-hex-character-escape.patch \
    tools/dev/pr72-subtract-assignment.patch; do
    if test -f "$patch"; then
        git apply --check --recount "$patch"
        git apply --recount "$patch"
    fi
done

if test -f tools/dev/pr71-update-platform.py; then
    python3 tools/dev/pr71-update-platform.py
fi
if test -f tools/dev/pr72-update-platform.py; then
    python3 tools/dev/pr72-update-platform.py
fi
if test -f tools/dev/pr72-post-platform.py; then
    python3 tools/dev/pr72-post-platform.py
fi
if test -f tools/dev/pr72-floating-unary.py; then
    python3 tools/dev/pr72-floating-unary.py
fi
if test -f tools/dev/pr72-record-layout-order.py; then
    python3 tools/dev/pr72-record-layout-order.py
fi
if test -f tools/dev/pr72-qualified-conditional.py; then
    python3 tools/dev/pr72-qualified-conditional.py
fi

printf '%s\n' 'staged Parson discovery semantics'
