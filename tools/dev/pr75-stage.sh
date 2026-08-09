#!/bin/sh
set -eu

python3 tools/dev/pr75-signed-char.py
python3 tools/dev/pr75-incomplete-extern-array.py
printf '%s\n' 'staged Lua discovery semantics'
