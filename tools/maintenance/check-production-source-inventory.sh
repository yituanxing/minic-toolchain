#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=${TMPDIR:-/tmp}/minic-production-source-inventory.$$
compiler_raw="$tmp/compiler.raw"
driver_raw="$tmp/driver.raw"
listed="$tmp/listed"
actual="$tmp/actual"

cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$tmp"

make --no-print-directory -s -f "$root/Makefile" \
    --eval='print-minic-sources:;@printf "%s\n" $(MINIC_SOURCES)' \
    print-minic-sources >"$compiler_raw"
make --no-print-directory -s -f "$root/Makefile" \
    --eval='print-minic-driver-sources:;@printf "%s\n" $(MINIC_DRIVER_SOURCES)' \
    print-minic-driver-sources >"$driver_raw"

if test ! -s "$compiler_raw" || test ! -s "$driver_raw"; then
    printf '%s\n' \
        "FAIL production-source-inventory: compiler or driver source list is empty" >&2
    exit 1
fi

for manifest in "$compiler_raw" "$driver_raw"; do
    LC_ALL=C sort "$manifest" >"$tmp/sorted"
    LC_ALL=C sort -u "$manifest" >"$tmp/unique"
    if ! cmp -s "$tmp/sorted" "$tmp/unique"; then
        printf '%s\n' \
            "FAIL production-source-inventory: duplicate in $manifest" >&2
        diff -u "$tmp/unique" "$tmp/sorted" >&2 || true
        exit 1
    fi
done
# The target catalog and driver-stage recipes may be linked into both tools.
# They are two intentional owners of one source, not duplicate entries within
# either explicit Makefile manifest.
LC_ALL=C sort -u "$compiler_raw" "$driver_raw" >"$listed"

while IFS= read -r source; do
    case "$source" in
    src/*.c|tools/minic/*.c|tools/minic-cc/*.c)
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
    find src tools/minic tools/minic-cc -type f -name '*.c' -print
) | LC_ALL=C sort >"$actual"

if ! cmp -s "$listed" "$actual"; then
    printf '%s\n' \
        "FAIL production-source-inventory: Makefile and repository sources differ" >&2
    diff -u "$listed" "$actual" >&2 || true
    exit 1
fi

count=$(wc -l <"$actual" | tr -d ' ')
printf '%s\n' \
    "PASS production-source-inventory sources=$count boundary=src,tools/minic-cc,tools/minic"
