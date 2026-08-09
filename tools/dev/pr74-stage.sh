#!/bin/sh
set -eu

python3 tools/dev/pr74-packed-record.py
python3 tools/dev/pr74-flexible-array-member.py
python3 tools/dev/pr74-inline-function.py
python3 tools/dev/pr74-postfix-const.py
printf '%s\n' 'staged SDS discovery semantics'
