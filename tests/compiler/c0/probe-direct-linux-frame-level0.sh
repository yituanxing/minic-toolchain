#!/usr/bin/env bash
# Actual Linux 6.6.143 arch/loongarch/kernel/stacktrace.c and process.c use
# __builtin_frame_address(0); arch/arm64/kernel/stacktrace.c likewise.
# Core owns builtin identity, target emits current aligned FIXED frame.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
target=${2:?aarch64 or loongarch64}
test -x "$mini"
case "$target" in
  aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64; expected='mov x9, sp' ;;
  loongarch64) cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64; expected='move $t0, $sp' ;;
  *) echo "unexpected ISA $target" >&2; exit 1 ;;
esac
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat > "$d/frame.i" <<'SRC'
void *mini_frame_address(void) { return __builtin_frame_address(0); }
unsigned long mini_frame_alignment(void) {
    return ((unsigned long)__builtin_frame_address(0)) & 15UL;
}
SRC
"$mini" --target="$target-linux" -S "$d/frame.i" -o "$d/frame.s"
grep -Fq "$expected" "$d/frame.s"
cat > "$d/bad.i" <<'SRC'
void *bad_frame(void) { return __builtin_frame_address(1); }
SRC
if "$mini" --target="$target-linux" -S "$d/bad.i" -o "$d/bad.s" >"$d/err" 2>&1; then
    echo "FAIL: $target admitted parent-frame builtin without frame chain" >&2
    exit 1
fi
test ! -e "$d/bad.s"
if [[ "${MINIC_FRAME_SOURCE_ONLY:-0}" != 1 ]]; then
    command -v "$cc" >/dev/null
    command -v "$qemu" >/dev/null
    "$cc" -c -x assembler "$d/frame.s" -o "$d/frame.o"
    cat > "$d/main.c" <<'SRC'
extern void *mini_frame_address(void);
extern unsigned long mini_frame_alignment(void);
int main(void) {
    unsigned char marker;
    unsigned long address=(unsigned long)mini_frame_address();
    unsigned long origin=(unsigned long)&marker;
    unsigned long diff=address>origin?address-origin:origin-address;
    if (!address || diff>4096U || mini_frame_alignment())return 1;
    return 0;
}
SRC
    "$cc" -static -O0 "$d/main.c" "$d/frame.o" -o "$d/run.elf"
    "$qemu" "$d/run.elf"
fi
echo "MINIC_LINUX_FRAME_LEVEL0=PASS target=$target parent_level=rejected current_frame=1"
