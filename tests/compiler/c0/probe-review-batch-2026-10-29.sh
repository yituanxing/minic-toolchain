#!/usr/bin/env bash
# One reproducible native + five-target source gate for the full-source
# architecture review batch. Runtime checks requiring non-host QEMU remain
# separate and are never silently treated as PASS here.
set -Eeuo pipefail
mini=${1:?Expected absolute MiniC compiler path}
test -x "$mini"
export MINIC_CC="$mini" MINIC="$mini" HOST_CC="${HOST_CC:-cc}"

sh tests/compiler/c0/probe-float32-const-eval.sh
sh tests/compiler/c0/probe-runtime-designated-positional.sh
sh tests/compiler/c0/probe-inline-symbolic-shared-body.sh
sh tests/compiler/c0/probe-rv32-ffsll-high-word.sh
sh tests/compiler/c0/probe-rv64-record-load-irregular.sh
sh tests/compiler/c0/probe-rv64-record-load-packed.sh
sh tests/compiler/c0/run-core-cfg-fold-effectful-conditions.sh
bash tests/compiler/c0/probe-core-expr-branch-local-facts.sh "$mini"
sh tests/compiler/c0/run-core-switch-case-fact-isolation.sh
sh tests/compiler/c0/run-core-rv32-va-record-cursor.sh
sh tests/compiler/c0/probe-rv32-variadic-record-abi-alignment.sh
MINIC="$(dirname "$mini")/minic" sh tests/compiler/c0/run-enum-tag-scopes.sh
MINIC="$(dirname "$mini")/minic" sh tests/compiler/c0/run-tag-namespace-scopes.sh
sh tests/compiler/c0/probe-external-pointer-array-initializers.sh
sh tests/compiler/c0/probe-gnu-cleanup-goto-lifetime.sh
bash tests/compiler/c0/probe-core-asm-goto-percent-escape.sh
bash tests/compiler/c0/probe-core-asm-empty-rk.sh
bash tests/compiler/c0/probe-core-asm-nonvolatile-rk.sh
bash tests/compiler/c0/probe-core-asm-zero-stride.sh
sh tests/compiler/c0/probe-rv32-target-gate.sh
bash tests/compiler/c0/probe-aarch64-typed-frame-path.sh "$mini"

echo 'MINIC_REVIEW_BATCH=PASS native_oracles=1 five_target_source=1 no_runtime_overclaim=1'
