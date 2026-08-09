#!/bin/sh
set -eu

python3 tools/dev/pr73-function-pointer-parameter.py
python3 tools/dev/pr73-extern-object.py
printf '%s\n' 'staged linenoise discovery semantics'
