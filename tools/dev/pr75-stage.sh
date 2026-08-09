#!/bin/sh
set -eu

python3 tools/dev/pr75-signed-char.py
printf '%s\n' 'staged Lua discovery semantics'
