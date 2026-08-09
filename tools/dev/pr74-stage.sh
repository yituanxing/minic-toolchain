#!/bin/sh
set -eu

python3 tools/dev/pr74-packed-record.py
printf '%s\n' 'staged SDS discovery semantics'
