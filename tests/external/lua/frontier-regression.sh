#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
min_passed=${LUA_FRONTIER_MIN_PASSED:-0}
first_source=${LUA_FRONTIER_FIRST_SOURCE:-lapi.c}
min_first_line=${LUA_FRONTIER_MIN_FIRST_LINE:-1650}
log=$(mktemp)
trap 'rm -f "$log"' EXIT HUP INT TERM

set +e
sh "$root/tests/external/lua/probe.sh" >"$log" 2>&1
status=$?
set -e

cat "$log"

if test "$status" -eq 0; then
    printf '%s\n' \
        "PASS external/lua-frontier regression=none completion=full"
    exit 0
fi

blocker=$(grep '^LUA_BLOCKER ' "$log" | tail -n 1 || true)
if test -z "$blocker"; then
    printf '%s\n' \
        "FAIL external/lua-frontier: probe failed without a structured LUA_BLOCKER" >&2
    exit "$status"
fi

source=$(printf '%s\n' "$blocker" | sed -n 's/.*source=\([^ ]*\).*/\1/p')
passed=$(printf '%s\n' "$blocker" | sed -n 's/.*passed=\([0-9][0-9]*\).*/\1/p')
line=$(printf '%s\n' "$blocker" | sed -n 's/.*line=\([0-9][0-9]*\).*/\1/p')

case "$passed" in
    ''|*[!0-9]*)
        printf '%s\n' \
            "FAIL external/lua-frontier: malformed passed count: $blocker" >&2
        exit 1
        ;;
esac
case "$line" in
    ''|*[!0-9]*)
        printf '%s\n' \
            "FAIL external/lua-frontier: malformed line number: $blocker" >&2
        exit 1
        ;;
esac
if test -z "$source"; then
    printf '%s\n' \
        "FAIL external/lua-frontier: malformed source name: $blocker" >&2
    exit 1
fi

if test "$passed" -gt "$min_passed"; then
    printf '%s\n' \
        "PASS external/lua-frontier regression=none passed=$passed source=$source line=$line baseline-passed=$min_passed"
    exit 0
fi

if test "$passed" -eq "$min_passed" && \
   test "$source" = "$first_source" && \
   test "$line" -ge "$min_first_line"; then
    printf '%s\n' \
        "PASS external/lua-frontier regression=none passed=$passed source=$source line=$line baseline=$first_source:$min_first_line"
    exit 0
fi

printf '%s\n' \
    "FAIL external/lua-frontier: regressed blocker=$source:$line passed=$passed baseline=$first_source:$min_first_line baseline-passed=$min_passed" >&2
exit 1
