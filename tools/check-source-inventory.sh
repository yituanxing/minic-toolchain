#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=${TMPDIR:-/tmp}/minic-source-inventory-$$
listed="$temporary/listed"
listed_sorted="$temporary/listed.sorted"
actual="$temporary/actual"

cleanup() {
    rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$temporary"
cd "$root"

make --no-print-directory print-minic-sources >"$listed"
if test ! -s "$listed"; then
    printf '%s\n' "FAIL source inventory: MINIC_SOURCES is empty" >&2
    exit 1
fi

duplicates=$(LC_ALL=C sort "$listed" | uniq -d)
if test -n "$duplicates"; then
    printf '%s\n' "FAIL source inventory: duplicate Makefile entries" >&2
    printf '%s\n' "$duplicates" >&2
    exit 1
fi

while IFS= read -r source; do
    case "$source" in
    src/*.c|src/*/*.c|src/*/*/*.c|tools/*.c|tools/*/*.c|tools/*/*/*.c)
        ;;
    *)
        printf '%s\n' \
            "FAIL source inventory: unexpected production source path $source" >&2
        exit 1
        ;;
    esac
    if test ! -f "$source"; then
        printf '%s\n' \
            "FAIL source inventory: listed source does not exist: $source" >&2
        exit 1
    fi
done <"$listed"

LC_ALL=C sort "$listed" >"$listed_sorted"
find src tools -type f -name '*.c' -print | LC_ALL=C sort >"$actual"

if ! cmp -s "$listed_sorted" "$actual"; then
    printf '%s\n' \
        "FAIL source inventory: MINIC_SOURCES and production C files differ" >&2
    diff -u "$listed_sorted" "$actual" >&2 || true
    exit 1
fi

count=$(wc -l <"$listed" | tr -d ' ')
printf '%s\n' "PASS source inventory entries=$count"
