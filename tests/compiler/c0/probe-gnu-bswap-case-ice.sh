#!/usr/bin/env bash
set -Eeuo pipefail
cc=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/source.i" <<'EOF'
typedef unsigned short be16;
typedef unsigned int be32;
typedef unsigned long long be64;
static int ip_protocol(be16 protocol) {
    switch (protocol) {
    case ((be16)(unsigned short)__builtin_bswap16((unsigned short)0x0800)):
        return 7;
    case ((be16)(unsigned short)__builtin_bswap16((unsigned short)0x86dd)):
        return 11;
    default: return 1;
    }
}
static int upper32(be32 x) {
    switch(x) {
    case (be32)__builtin_bswap32((be32)0x12345678U): return 13;
    default: return 1;
    }
}
static int upper64(be64 x) {
    switch(x) {
    case (be64)__builtin_bswap64((be64)0x0102030405060708ULL): return 11;
    default: return 1;
    }
}
int main(void) {
    if (ip_protocol((be16)0x0008U) != 7 ||
        ip_protocol((be16)0xdd86U) != 11 ||
        upper32((be32)0x78563412U) != 13 ||
        upper64((be64)0x0807060504030201ULL) != 11)
        return 1;
    return 42;
}
EOF
"$cc" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
cc -no-pie -x assembler "$work/minic.s" -o "$work/minic.elf"
cc -std=gnu11 -no-pie -x c "$work/source.i" -o "$work/gnu.elf"
for mode in minic gnu; do
    set +e
    "$work/$mode.elf"
    actual=$?
    set -e
    test "$actual" -eq 42 || {
      echo "FAIL: $mode byte-swap case-label result $actual" >&2
      exit 1
    }
done
cat >"$work/dynamic.i" <<'EOF'
unsigned short bad(unsigned short value) {
    switch(value) {
    case __builtin_bswap16(value): return 1;
    default: return 0;
    }
}
EOF
if "$cc" --target=x86_64-linux -S "$work/dynamic.i" \
    -o "$work/dynamic.s" >"$work/dynamic.log" 2>&1; then
    echo 'FAIL: nonconstant bswap allowed in switch label' >&2
    exit 1
fi
grep -F 'case label currently requires an integer constant expression' "$work/dynamic.log" >/dev/null
echo 'GNU_BSWAP_CASE_ICE_GCC_NATIVE=PASS 16bit=2 32bit=1 64bit=1 dynamic_rejected=1'
