#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_rng_user_cache.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/owner.i"
"$mini" --target=aarch64-linux -S "$work/owner.i" -o "$work/owner.s"
grep -Fq '.inst 0xd53b2409' "$work/owner.s"
grep -Fq '.inst 0xd53b2429' "$work/owner.s"
test "$(grep -Fc 'cset w10, ne' "$work/owner.s")" -eq 2
test "$(grep -Fc '.short 1001' "$work/owner.s")" -eq 4
grep -Fq '1: dc civac, x10' "$work/owner.s"
grep -Fq '1: sys 3, c7, c13, 1, x10' "$work/owner.s"
grep -Fq '1: sys 3, c7, c12, 1, x10' "$work/owner.s"
grep -Fq '1: ic ivau, x10' "$work/owner.s"

# The target must not accept a changed NZCV predicate, a third RNG register
# selector, a missing cc clobber, a changed cache opcode, a different fixup
# label, or a forged exception-entry type.
sed 's/cset %w1, ne/cset %w1, eq/' "$src" >"$work/rng-condition.c"
sed 's/RNG_BODY("1")/RNG_BODY("2")/' "$src" >"$work/rng-encoding.c"
sed 's/: : "cc"/: : "memory"/' "$src" >"$work/rng-clobber.c"
sed 's/CACHE_FUNCTION(linux_dc_civac, "dc civac")/CACHE_FUNCTION(linux_dc_civac, "dc cvac")/' \
    "$src" >"$work/cache-opcode.c"
sed 's/\.long ((2b) - \.)/\.long ((3b) - .)/' "$src" >"$work/cache-fixup.c"
sed 's/\.short (2)/\.short (3)/' "$src" >"$work/cache-type.c"
for bad in rng-condition rng-encoding rng-clobber cache-opcode cache-fixup cache-type; do
    "$host" -E -P -std=gnu11 "$work/$bad.c" -o "$work/$bad.i"
    if "$mini" --target=aarch64-linux -S "$work/$bad.i" \
        -o "$work/$bad.s" >"$work/$bad.out" 2>"$work/$bad.err"; then
        echo "AARCH64_RNG_CACHE_FAULT=FAIL accepted=$bad" >&2
        exit 1
    fi
    test ! -s "$work/$bad.s"
done

if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/owner.s" -o "$work/owner.o"
    aarch64-linux-gnu-readelf -SW "$work/owner.o" | grep -Fq __ex_table
    aarch64-linux-gnu-readelf -rW "$work/owner.o" >"$work/relocs"
    count=$(grep -c R_AARCH64_PREL32 "$work/relocs")
    test "$count" -ge 8
    echo "AARCH64_RNG_CACHE_FAULT=PASS rng=2 cache=4 negatives=6 PREL32=$count GNU_AS=PASS"
else
    echo 'AARCH64_RNG_CACHE_FAULT=PASS rng=2 cache=4 negatives=6 GNU_AS=SKIP(toolchain-unavailable)'
fi
