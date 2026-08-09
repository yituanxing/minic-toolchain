#!/bin/sh
set -eu

sh tools/dev/pr75-stage.sh
python3 tools/dev/pr76-preprocessed-line-markers.py
python3 tools/dev/pr76-anonymous-record-members.py
printf '%s\n' 'staged Linux discovery semantics'
