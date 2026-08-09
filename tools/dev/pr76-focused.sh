#!/bin/sh
set -eu

sh tools/dev/pr75-focused.sh
sh tests/compiler/c0/run-preprocessed-line-markers.sh
sh tests/compiler/c0/run-gnu-signed-keyword.sh
sh tests/compiler/c0/run-anonymous-record-members.sh
