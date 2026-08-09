#!/bin/sh
set -eu

sh tools/dev/pr75-stage.sh
python3 tools/dev/pr76-preprocessed-line-markers.py
python3 tools/dev/pr77-gnu-signed-keyword.py
python3 tools/dev/pr77-gnu-int128-type.py
python3 tools/dev/pr77-gnu-int128-contracts.py
python3 tools/dev/pr77-bool-type.py
python3 tools/dev/pr77-bool-constant-cast.py
python3 tools/dev/pr77-gnu-typedef-redundant-aligned.py
python3 tools/dev/pr77-gnu-record-alignment.py
python3 tools/dev/pr77-gnu-empty-records.py
python3 tools/dev/pr77-gnu-extension-prefix.py
python3 tools/dev/pr77-gnu-prefix-function-attributes.py
python3 tools/dev/pr77-stabilize-ast-anchors.py
python3 tools/dev/pr76-anonymous-record-members.py
python3 tools/dev/pr77-fix-generated-nul.py
printf '%s\n' 'staged Linux discovery semantics'
