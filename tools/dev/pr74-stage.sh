#!/bin/sh
set -eu

python3 tools/dev/pr74-packed-record.py
python3 tools/dev/pr74-flexible-array-member.py
printf '%s\n' 'staged SDS discovery semantics'
