#!/bin/sh
set -eu

python3 tools/dev/pr73-function-pointer-parameter.py
python3 tools/dev/pr73-extern-object.py
python3 tools/dev/pr73-function-type-typedef.py
python3 tools/dev/pr73-unnamed-prototype-parameters.py
python3 tools/dev/pr73-static-pointer-array.py
python3 tools/dev/pr73-static-zero-definition.py
python3 tools/dev/pr73-record-copy-array-members.py
python3 tools/dev/pr73-zero-aggregate-null.py
python3 tools/dev/pr73-record-local-initializer.py
printf '%s\n' 'staged linenoise discovery semantics'
