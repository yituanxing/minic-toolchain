#!/bin/sh
set -eu

python3 tools/dev/pr75-signed-char.py
python3 tools/dev/pr75-incomplete-extern-array.py
python3 tools/dev/pr75-incomplete-array-verifier.py
python3 tools/dev/pr75-volatile-qualifier.py
python3 tools/dev/pr75-pointer-volatile-qualifier.py
python3 tools/dev/pr75-array-bound-sizeof.py
python3 tools/dev/pr75-array-bound-enum.py
python3 tools/dev/pr75-comma-operator.py
python3 tools/dev/pr75-cast-type-classifier.py
python3 tools/dev/pr75-extern-function-declarations.py
python3 tools/dev/pr75-anonymous-record-type-specifiers.py
python3 tools/dev/pr75-union-local-declaration.py
python3 tools/dev/pr75-enum-typedef.py
python3 tools/dev/pr75-record-forward-declarations.py
python3 tools/dev/pr75-record-top-level-dispatch.py
python3 tools/dev/pr75-record-multi-declarators.py
python3 tools/dev/pr75-record-multidimensional-arrays.py
python3 tools/dev/pr75-lua-shim-simplify.py
printf '%s\n' 'staged Lua discovery semantics'
