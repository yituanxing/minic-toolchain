#!/bin/sh
set -eu

sh tools/dev/pr75-focused.sh
sh tests/compiler/c0/run-preprocessed-line-markers.sh
sh tests/compiler/c0/run-gnu-signed-keyword.sh
sh tests/compiler/c0/run-gnu-int128-type.sh
sh tests/compiler/c0/run-bool-semantics.sh
sh tests/compiler/c0/run-typeof-generic.sh
sh tests/compiler/c0/run-gnu-typedef-redundant-aligned.sh
sh tests/compiler/c0/run-gnu-record-alignment.sh
sh tests/compiler/c0/run-gnu-empty-records.sh
sh tests/compiler/c0/run-gnu-extension-prefix-declarations.sh
sh tests/compiler/c0/run-gnu-prefix-function-attributes.sh
sh tests/compiler/c0/run-anonymous-record-members.sh
sh tests/compiler/c0/run-block-scope-extern-function-attributes.sh
sh tests/compiler/c0/run-gnu-void-pointer-arithmetic.sh
sh tests/compiler/c0/run-gnu-statement-expression.sh
sh tests/compiler/c0/run-gnu-inline-asm-fence.sh
sh tests/compiler/c0/run-gnu-typeof-local-declaration.sh
sh tests/compiler/c0/run-gnu-inline-asm-readwrite-output.sh
sh tests/compiler/c0/run-gnu-compiletime-selection-builtins.sh
sh tests/compiler/c0/run-rv64-integer-aggregate-return.sh
sh tests/compiler/c0/run-unnamed-bit-fields.sh
sh tests/compiler/c0/run-gnu-overflow-builtins.sh
sh tests/compiler/c0/run-gnu-register-inline-asm-output.sh
