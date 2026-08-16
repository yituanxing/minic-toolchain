#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
exec sh "$root/tests/compiler/c0/run-gnu-local-fixed-register-bindings.sh"
