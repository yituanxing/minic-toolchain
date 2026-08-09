#!/bin/sh
set -eu

python3 tools/dev/pr75-signed-char.py
python3 tools/dev/pr75-incomplete-extern-array.py
python3 tools/dev/pr75-incomplete-array-verifier.py
python3 tools/dev/pr75-extern-function-declarations.py
python3 tools/dev/pr75-anonymous-record-type-specifiers.py
printf '%s\n' 'staged Lua discovery semantics'
