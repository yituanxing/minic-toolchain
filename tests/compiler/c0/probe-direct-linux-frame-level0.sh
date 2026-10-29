#!/usr/bin/env bash
# LoongArch Linux uses level 0. Arm64 Linux 6.6 stacktrace calls
# __builtin_frame_address(1), requiring the genuine x29 caller record.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
target=${2:?aarch64 or loongarch64}
test -x "$mini"
case "$target" in
  aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64; expected='mov x9, x29' ;;
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
if [[ "$target" == aarch64 ]]; then
cat >> "$d/frame.i" <<'SRC'
void *mini_parent_frame(void) { return __builtin_frame_address(1); }
void *mini_dynamic_parent_frame(unsigned long n) {
    volatile unsigned char *p = __builtin_alloca(n);
    p[0] = 1;
    return __builtin_frame_address(1);
}
void *mini_overaligned_parent_frame(unsigned long a, unsigned long b,
    unsigned long c, unsigned long d, unsigned long e, unsigned long f,
    unsigned long g, unsigned long h, unsigned long ninth) {
    volatile unsigned char slot[32] __attribute__((aligned(32)));
    slot[0] = (unsigned char)ninth;
    return __builtin_frame_address(1);
}
SRC
fi
"$mini" --target="$target-linux" -S "$d/frame.i" -o "$d/frame.s"
grep -Fq "$expected" "$d/frame.s"
if [[ "$target" == aarch64 ]]; then
    grep -Fq 'stp x29, x30, [sp, #-16]!' "$d/frame.s"
    grep -Fq 'ldr x9, [x29]' "$d/frame.s"
    grep -Fq 'ldp x29, x30, [sp], #16' "$d/frame.s"
fi
cat > "$d/bad.i" <<'SRC'
void *bad_frame(void) { return __builtin_frame_address(2); }
SRC
if [[ "$target" != aarch64 ]]; then
    sed -i 's/frame_address(2)/frame_address(1)/' "$d/bad.i"
fi
if "$mini" --target="$target-linux" -S "$d/bad.i" -o "$d/bad.s" >"$d/err" 2>&1; then
    echo "FAIL: $target admitted an unsupported frame-chain depth" >&2
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
extern void *mini_parent_frame(void);
extern void *mini_dynamic_parent_frame(unsigned long);
extern void *mini_overaligned_parent_frame(unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long, unsigned long,
    unsigned long, unsigned long, unsigned long);
int main(void) {
    unsigned char marker;
    unsigned long address=(unsigned long)mini_frame_address();
    unsigned long origin=(unsigned long)&marker;
    unsigned long diff=address>origin?address-origin:origin-address;
    if (!address || diff>4096U || mini_frame_alignment())return 1;
#if defined(__aarch64__)
    void *caller = __builtin_frame_address(0);
    if (!caller || mini_parent_frame() != caller ||
        mini_dynamic_parent_frame(40U) != caller ||
        mini_overaligned_parent_frame(1,2,3,4,5,6,7,8,9) != caller)
        return 2;
#endif
    return 0;
}
SRC
    "$cc" -static -O0 -fno-omit-frame-pointer "$d/main.c" "$d/frame.o" -o "$d/run.elf"
    "$qemu" "$d/run.elf"
fi
echo "MINIC_LINUX_FRAME_LEVEL0=PASS target=$target valid_parent_level=$([[ "$target" == aarch64 ]] && echo 1 || echo 0) current_frame=1"
