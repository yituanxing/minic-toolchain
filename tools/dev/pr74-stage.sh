#!/bin/sh
set -eu

python3 tools/dev/pr74-packed-record.py
python3 tools/dev/pr74-flexible-array-member.py
python3 tools/dev/pr74-inline-function.py
python3 tools/dev/pr74-postfix-const.py
python3 tools/dev/pr74-long-long.py
python3 tools/dev/pr74-wide-integer-literals.py
python3 tools/dev/pr74-array-bound-constant-expression.py
python3 tools/dev/pr74-void-pointer-local.py
python3 tools/dev/pr74-divide-assignment-token.py
python3 tools/dev/pr74-compound-assignment-expression.py
python3 tools/dev/pr74-divide-assignment-extension.py
python3 tools/dev/pr74-void-cast.py
python3 tools/dev/pr74-external-scalar-definition.py
printf '%s\n' 'staged SDS discovery semantics'
