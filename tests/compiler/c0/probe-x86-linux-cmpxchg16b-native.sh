#!/usr/bin/env bash
# True hardware CMPXCHG16B native differential + Linux GS alternative ELF ABI.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'X86_CMPXCHG16B=FAIL reason=%s\n' "$*" >&2; exit 1; }
test -x "$mini" || fail 'compiler missing'

for kind in atomic alt; do
    source="$root/tests/compiler/c0/x86_linux_cmpxchg16b_$kind.c"
    cc -std=gnu11 -E -P -x c "$source" -o "$work/$kind.i"
    "$mini" --target=x86_64-linux -S "$work/$kind.i" -o "$work/$kind.s" \
        || fail "$kind MiniC frontend/Core/M1"
    cc -std=gnu11 -O0 -fno-pie -c -x c "$work/$kind.i" -o "$work/gnu-$kind.o" \
        || fail "$kind GNU C oracle"
    cc -c -x assembler "$work/$kind.s" -o "$work/mini-$kind.o" \
        || fail "$kind GNU GAS"
done

# Only the locked memory CAS is safe and meaningful in native user space.
cc -no-pie "$work/gnu-atomic.o" -o "$work/gnu-atomic"
cc -no-pie "$work/mini-atomic.o" -o "$work/mini-atomic"
set +e
"$work/gnu-atomic"; gnu_status=$?
"$work/mini-atomic"; mini_status=$?
set -e
test "$gnu_status" -eq 42 || fail "GNU real hardware returned $gnu_status"
test "$mini_status" -eq 42 || fail "MiniC real hardware returned $mini_status"
rg -q 'lock; cmpxchg16b \(%r11\)' "$work/atomic.s" \
    || fail 'locked CAS machine instruction absent'
rg -q 'sete %r10b' "$work/atomic.s" \
    || fail 'ZF boolean must be captured immediately'

# ALT record is exactly 14 bytes: two PC-relative offsets, feature 141,
# and the real old-branch/new-branch lengths 8/5. Both GNU and MiniC GAS
# must produce the same Linux metadata. The replacement itself is not run.
for compiler in gnu mini; do
    obj="$work/$compiler-alt.o"
    readelf -SW "$obj" > "$work/$compiler-sections"
    rg -q '\.altinstructions' "$work/$compiler-sections" || fail "$compiler ALT section"
    objcopy --dump-section .altinstructions="$work/$compiler-alt.bin" "$obj"
    test "$(wc -c < "$work/$compiler-alt.bin")" -eq 14 \
        || fail "$compiler ALT record must be 14 bytes"
    bytes=$(od -An -tx1 "$work/$compiler-alt.bin" | tr -d ' \n')
    case "$bytes" in
        *8d0000000805) ;;
        *) fail "$compiler ALT patch must encode feature141 lengths8/5" ;;
    esac
    readelf -Wr "$obj" > "$work/$compiler-relocations"
    relocs=$(awk '/^Relocation section/ { in_alt = index($0, ".rela.altinstructions") != 0; next }
        in_alt && /R_X86_64_PC32/ { count++ } END { print count + 0 }' \
        "$work/$compiler-relocations")
    test "$relocs" -eq 2 || fail "$compiler ALT expected two PC32 relocations, got $relocs"
done
rg -q 'call this_cpu_cmpxchg16b_emu' "$work/alt.s" \
    || fail 'old ALT private kernel emulation call absent'
rg -q 'cmpxchg16b %gs:\(%r11\)' "$work/alt.s" \
    || fail 'GS replacement exact memory operand absent'
printf '%s\n' 'X86_CMPXCHG16B=PASS GNU_status=42 MiniC_status=42 locked=real_128bit_atomic ALT_feature=141 ALT_old=8 ALT_new=5 ALT_relocs=2*PC32 ALT_execute=never'
