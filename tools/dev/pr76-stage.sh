#!/bin/sh
set -eu

sh tools/dev/pr75-stage.sh
python3 tools/dev/pr76-preprocessed-line-markers.py
python3 tools/dev/pr77-gnu-signed-keyword.py
python3 tools/dev/pr77-gnu-int128-type.py
python3 tools/dev/pr77-gnu-int128-contracts.py
python3 tools/dev/pr77-bool-type.py
python3 tools/dev/pr77-bool-constant-cast.py
python3 tools/dev/pr77-typeof-generic.py
python3 tools/dev/pr77-type-name-lookahead-fix.py
python3 tools/dev/pr77-gnu-typedef-redundant-aligned.py
python3 tools/dev/pr77-gnu-record-alignment.py
python3 tools/dev/pr77-gnu-empty-records.py
python3 tools/dev/pr77-gnu-extension-prefix.py
python3 tools/dev/pr77-gnu-prefix-function-attributes.py
python3 tools/dev/pr77-stabilize-ast-anchors.py
python3 tools/dev/pr76-anonymous-record-members.py
python3 tools/dev/pr77-fix-generated-nul.py
python3 tools/dev/pr77-block-scope-extern-functions.py
python3 tools/dev/pr77-gnu-void-pointer-arithmetic.py
python3 tools/dev/pr77-gnu-statement-expressions-v2.py
python3 tools/dev/pr77-gnu-statement-expression-comma-compat.py
python3 tools/dev/pr77-gnu-inline-asm.py
python3 tools/dev/pr77-gnu-inline-asm-escape-fix.py
python3 tools/dev/pr77-shared-local-declaration-lookahead.py
python3 tools/dev/pr77-gnu-inline-asm-readwrite-output.py
python3 tools/dev/pr77-gnu-compiletime-selection-builtins.py
python3 tools/dev/pr77-rv64-integer-aggregate-return.py
python3 tools/dev/pr77-rv64-integer-aggregate-return-escape-fix.py
python3 tools/dev/pr77-rv64-integer-aggregate-call.py
python3 tools/dev/pr77-unnamed-bit-fields.py
printf '%s\n' 'staged Linux discovery semantics'
