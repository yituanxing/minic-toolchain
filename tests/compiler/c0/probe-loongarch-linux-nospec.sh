#!/usr/bin/env bash
# Genuine Linux 6.6.143 LoongArch array_index_mask_nospec() exact GNU ISA.
# Independent GNU producer + MiniC -S + real target QEMU, 0 and ~0 compare.
set -Eeuo pipefail
mini=${1:?MiniC compiler binary}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/nospec.i" <<'SRC'
unsigned long mini_nospec(unsigned long index, unsigned long size) {
    unsigned long mask;
    __asm__ __volatile__(
        "sltu\t%0, %1, %2\n\t"
        "sub.d\t%0, $zero, %0\n\t"
        : "=r"(mask) : "r"(index), "r"(size) :);
    return mask;
}
struct __attribute__((packed)) MiniMaskResult {
    unsigned char pad;
    unsigned long mask;
};
void mini_nospec_packed(struct MiniMaskResult *p,
                        unsigned long index, unsigned long size) {
    __asm__ __volatile__(
        "sltu\t%0, %1, %2\n\t"
        "sub.d\t%0, $zero, %0\n\t"
        : "=r"(p->mask) : "r"(index), "r"(size) :);
}
SRC
"$mini" --target=loongarch64-linux -S "$d/nospec.i" -o "$d/mini.s"
# Source-authored GNU inline ASM preserves tabs from the original template;
# the old direct emitter normalized them to spaces. Prove the same physical
# register roles and operations without depending on that whitespace choice.
grep -Eq '^[[:space:]]*sltu[[:space:]]+\$t2,[[:space:]]*\$t0,[[:space:]]*\$t1[[:space:]]*$' "$d/mini.s"
grep -Eq '^[[:space:]]*sub\.d[[:space:]]+\$t2,[[:space:]]*\$zero,[[:space:]]*\$t2[[:space:]]*$' "$d/mini.s"
# Source-form MachineIR preserves the exact GNU template, including tabs.
# Its three register operands must nevertheless be physically fixed to the
# original contract, not passed through as unbound text placeholders.
grep -Eq '^[[:blank:]]+sltu[[:blank:]]+\$t2, \$t0, \$t1$' "$d/mini.s"
grep -Eq '^[[:blank:]]+sub\.d[[:blank:]]+\$t2, \$zero, \$t2$' "$d/mini.s"
grep -Fq 'mini_nospec_packed:' "$d/mini.s"
grep -Fq '  andi $t3, $t1, 7' "$d/mini.s"
grep -Fq '  st.b $t0, $t1, 0' "$d/mini.s"
for bad in extra mnemonic constraint volatile clobber width; do
    case "$bad" in
        extra) sed 's/sub.d\\t%0, \$zero, %0/sub.d\\t%0, $zero, %0; nop/' "$d/nospec.i" > "$d/bad.i" ;;
        mnemonic) sed 's/sltu\\t%0, %1, %2/slt\\t%0, %1, %2/' "$d/nospec.i" > "$d/bad.i" ;;
        constraint) sed 's/: "=r"(mask)/: "=m"(mask)/' "$d/nospec.i" > "$d/bad.i" ;;
        volatile) sed 's/__asm__ __volatile__/__asm__/' "$d/nospec.i" > "$d/bad.i" ;;
        clobber) sed 's/: "r"(index), "r"(size) :);/: "r"(index), "r"(size) : "memory");/' "$d/nospec.i" > "$d/bad.i" ;;
        width) sed 's/unsigned long mask;/unsigned int mask;/' "$d/nospec.i" > "$d/bad.i" ;;
    esac
    if "$mini" --target=loongarch64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1; then
        echo "LOONG_MASK_NOSPEC=FAIL unsafe $bad accepted" >&2
        exit 1
    fi
    test ! -e "$d/bad.s"
done
if [[ "${MINIC_LOONG_NOSPEC_NATIVE_ONLY:-0}" != 1 ]]; then
    gnu=loongarch64-linux-gnu-gcc-13
    command -v "$gnu" >/dev/null
    command -v qemu-loongarch64 >/dev/null
    sed 's/mini_nospec/gnu_nospec/' "$d/nospec.i" > "$d/gnu.c"
    "$gnu" -O0 -c "$d/gnu.c" -o "$d/gnu.o"
    "$gnu" -c -x assembler "$d/mini.s" -o "$d/mini.o"
    cat > "$d/main.c" <<'SRC'
extern unsigned long mini_nospec(unsigned long, unsigned long);
extern unsigned long gnu_nospec(unsigned long, unsigned long);
struct __attribute__((packed)) MiniMaskResult {
    unsigned char pad;
    unsigned long mask;
};
extern void mini_nospec_packed(struct MiniMaskResult *, unsigned long,
                              unsigned long);
extern void gnu_nospec_packed(struct MiniMaskResult *, unsigned long,
                             unsigned long);
int main(void) {
    unsigned long i, j;
    for (i=0; i<128; ++i) for (j=0; j<128; ++j) {
        unsigned long actual=mini_nospec(i,j);
        unsigned long expected=gnu_nospec(i,j);
        struct MiniMaskResult mini={0,0}, gnu={0,0};
        mini_nospec_packed(&mini,i,j);
        gnu_nospec_packed(&gnu,i,j);
        if (actual!=expected || actual!=(i<j?~0UL:0UL)) return 1;
        if (mini.mask!=gnu.mask || mini.mask!=expected) return 2;
    }
    return 42;
}
SRC
    "$gnu" -static -O0 "$d/main.c" "$d/gnu.o" "$d/mini.o" -o "$d/run.elf"
    rc=0; qemu-loongarch64 "$d/run.elf" || rc=$?
    test "$rc" -eq 42
fi
echo "LOONG_MASK_NOSPEC=PASS original_linux=1 GNU_asm=1 negatives=6 QEMU_mode=${MINIC_LOONG_NOSPEC_NATIVE_ONLY:-0}"
