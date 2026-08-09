#!/bin/sh
set -eu

# Linux discovery is stacked on the Lua capability branch. Reuse Lua's full staged compiler,
# then add only Linux-specific language/input semantics here so this branch never edits the
# Lua staging contract itself.
sh tools/dev/pr75-stage.sh
python3 tools/dev/pr76-preprocessed-line-markers.py
python3 tools/dev/pr76-anonymous-record-members.py
printf '%s\n' 'staged Linux discovery semantics'
