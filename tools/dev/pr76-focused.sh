#!/bin/sh
set -eu

sh tools/dev/pr75-focused.sh
sh tests/compiler/c0/run-preprocessed-line-markers.sh
sh tests/compiler/c0/run-gnu-signed-keyword.sh
sh tests/compiler/c0/run-gnu-int128-type.sh
sh tests/compiler/c0/run-bool-semantics.sh
sh tests/compiler/c0/run-typeof-generic.sh
sh tests/compiler/c0/run-gnu-typedef-redundant-aligned.sh
sh tests/compiler/c0/run-gnu-record-alignment.sh
sh tests/compiler/c0/run-gnu-empty-records.sh
sh tests/compiler/c0/run-gnu-extension-prefix-declarations.sh
sh tests/compiler/c0/run-gnu-prefix-function-attributes.sh
sh tests/compiler/c0/run-anonymous-record-members.sh
