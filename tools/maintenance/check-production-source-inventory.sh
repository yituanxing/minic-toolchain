#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=${TMPDIR:-/tmp}/minic-production-source-inventory.$$
listed_raw="$tmp/listed.raw"
listed="$tmp/listed"
unique="$tmp/listed.unique"
actual="$tmp/actual"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$tmp"

make --no-print-directory -s -f "$root/Makefile" \
    --eval='print-minic-sources:;@printf "%s\n" $(MINIC_SOURCES)' \
    print-minic-sources >"$listed_raw"

if test ! -s "$listed_raw"; then
    printf '%s\n' \
        "FAIL production-source-inventory: MINIC_SOURCES is empty" >&2
    exit 1
fi

LC_ALL=C sort "$listed_raw" >"$listed"
LC_ALL=C sort -u "$listed_raw" >"$unique"
if ! cmp -s "$listed" "$unique"; then
    printf '%s\n' \
        "FAIL production-source-inventory: MINIC_SOURCES contains duplicates" >&2
    diff -u "$unique" "$listed" >&2 || true
    exit 1
fi

while IFS= read -r source; do
    case "$source" in
    src/*.c|tools/minic/*.c)
        ;;
    *)
        printf '%s\n' \
            "FAIL production-source-inventory: entry outside production boundary: $source" >&2
        exit 1
        ;;
    esac

    if test ! -f "$root/$source"; then
        printf '%s\n' \
            "FAIL production-source-inventory: listed source is missing: $source" >&2
        exit 1
    fi
done <"$listed"

(
    cd "$root"
    find src tools/minic -type f -name '*.c' -print
) | LC_ALL=C sort >"$actual"

if ! cmp -s "$listed" "$actual"; then
    printf '%s\n' \
        "FAIL production-source-inventory: Makefile and repository sources differ" >&2
    diff -u "$listed" "$actual" >&2 || true
    exit 1
fi

count=$(wc -l <"$actual" | tr -d ' ')
printf '%s\n' \
    "PASS production-source-inventory sources=$count boundary=src,tools/minic"
