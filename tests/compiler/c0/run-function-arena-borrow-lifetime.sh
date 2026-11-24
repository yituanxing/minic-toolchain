#!/bin/sh
# Compatibility entry point; original assertions live in the shared driver.
exec sh "$(dirname -- "$0")/run-compile-only-case.sh" function-arena-borrow-lifetime
