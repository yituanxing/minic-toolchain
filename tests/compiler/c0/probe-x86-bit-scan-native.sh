#!/usr/bin/env bash
# Original Linux 6.6.143 __ffs, __fls and ffz bit scan GNU asm contract.
# Compare MiniC output with GCC on nonzero inputs, never claim zero semantics.
set -Eeuo pipefail
minic=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/scans.i" <<'EOF'
unsigned long mini_ffs(unsigned long word) {
    asm("rep; bsf %1,%0" : "=r"(word) : "rm"(word));
    return word;
}
unsigned long mini_ffz(unsigned long word) {
    unsigned long result;
    asm("rep; bsf %1,%0" : "=r"(result) : "r"(~word));
    return result;
}
unsigned long mini_fls(unsigned long word) {
    asm("bsr %1,%0" : "=r"(word) : "rm"(word));
    return word;
}
EOF
cat >"$work/gcc.c" <<'EOF'
unsigned long gcc_ffs(unsigned long word) {
    asm("rep; bsf %1,%0" : "=r"(word) : "rm"(word));
    return word;
}
unsigned long gcc_ffz(unsigned long word) {
    unsigned long result;
    asm("rep; bsf %1,%0" : "=r"(result) : "r"(~word));
    return result;
}
unsigned long gcc_fls(unsigned long word) {
    asm("bsr %1,%0" : "=r"(word) : "rm"(word));
    return word;
}
EOF
cat >"$work/driver.c" <<'EOF'
extern unsigned long mini_ffs(unsigned long), mini_ffz(unsigned long);
extern unsigned long mini_fls(unsigned long);
extern unsigned long gcc_ffs(unsigned long), gcc_ffz(unsigned long);
extern unsigned long gcc_fls(unsigned long);
int main(void) {
    static const unsigned long cases[] = {
        1UL, 2UL, 4UL, 8UL, 255UL, 1024UL,
        (1UL << 31), (1UL << 47), (1UL << 63), ~0UL
    };
    unsigned long i, value;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        value = cases[i];
        if (mini_ffs(value) != gcc_ffs(value) ||
            mini_fls(value) != gcc_fls(value))
            return 1;
        if (value != ~0UL && mini_ffz(value) != gcc_ffz(value))
            return 2;
    }
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$work/scans.i" -o "$work/minic.s"
grep -F '  rep; bsfq %r10, %rax' "$work/minic.s" >/dev/null
grep -F '  bsrq %r10, %rax' "$work/minic.s" >/dev/null
cc -no-pie -O0 "$work/driver.c" "$work/gcc.c" \
    -x assembler "$work/minic.s" -o "$work/check.elf"
set +e
"$work/check.elf"
rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL x86 GNU bit scan differential rc=$rc" >&2; exit 1; }
cat >"$work/unsafe.i" <<'EOF'
unsigned long bad(unsigned long word) {
    asm("rep; bsf %1,%0; cli" : "=r"(word) : "rm"(word));
    return word;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/unsafe.i" \
    -o "$work/unsafe.s" >"$work/unsafe.log" 2>&1; then
    echo 'FAIL unknown x86 machine template admitted' >&2
    exit 1
fi
cat >"$work/wrong-width.i" <<'EOF'
unsigned int bad(unsigned int word) {
    asm("rep; bsf %1,%0" : "=r"(word) : "rm"(word));
    return word;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/wrong-width.i" \
    -o "$work/wrong-width.s" >"$work/wrong-width.log" 2>&1; then
    echo 'FAIL x86 32-bit bsf mistaken for native 64-bit contract' >&2
    exit 1
fi
echo 'X86_GNU_BIT_SCAN_NATIVE=PASS bsf_nonzero=10 bsr_nonzero=10 ffz_nonzero=9 unknown_machine_rejected=1 wrong_width_rejected=1'
