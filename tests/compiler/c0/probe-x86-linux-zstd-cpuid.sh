#!/usr/bin/env bash
# Linux 6.6 lib/zstd/common/cpu.h uses three nonvolatile CPUID layouts,
# including the single-EAX output with three register clobbers.
set -Eeuo pipefail
minic=$1; d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat >"$d/zstd.i" <<'EOF'
typedef unsigned int U32;
struct cpuid_info { U32 n, f1a, f1c, f1d, f7a, f7b, f7c; };
void mini_zstd_cpuid(struct cpuid_info *p) {
    U32 n=0, f1a=0, f1c=0, f1d=0, f7a=0, f7b=0, f7c=0;
    __asm__("cpuid" : "=a"(n) : "a"(0) : "ebx", "ecx", "edx");
    if (n >= 1)
        __asm__("cpuid" : "=a"(f1a), "=c"(f1c), "=d"(f1d)
                        : "a"(1) : "ebx");
    if (n >= 7)
        __asm__("cpuid" : "=a"(f7a), "=b"(f7b), "=c"(f7c)
                        : "a"(7), "c"(0) : "edx");
    p->n=n; p->f1a=f1a; p->f1c=f1c; p->f1d=f1d;
    p->f7a=f7a; p->f7b=f7b; p->f7c=f7c;
}
EOF
sed 's/mini_zstd_cpuid/gnu_zstd_cpuid/g' "$d/zstd.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
struct cpuid_info {
    unsigned int n, f1a, f1c, f1d, f7a, f7b, f7c;
};
extern void mini_zstd_cpuid(struct cpuid_info *);
extern void gnu_zstd_cpuid(struct cpuid_info *);
int main(void) {
    struct cpuid_info a={0},b={0};
    unsigned int i;
    mini_zstd_cpuid(&a);
    gnu_zstd_cpuid(&b);
    for (i=0; i<7; ++i)
        if (((const unsigned int *)&a)[i] != ((const unsigned int *)&b)[i])
            return 1;
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$d/zstd.i" -o "$d/zstd.s"
test "$(grep -c '^  cpuid$' "$d/zstd.s")" -eq 3
test "$(grep -c '^  pushq %rbx$' "$d/zstd.s")" -eq 3
test "$(grep -c '^  popq %rbx$' "$d/zstd.s")" -eq 3
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/zstd.s" -o "$d/diff"
set +e
"$d/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL ZSTD_cpuid GNU differential rc=$rc"; exit 1; }
cat >"$d/unsafe.i" <<'EOF'
void bad(unsigned int *eax) {
    asm("cpuid; cli" : "=a"(*eax) : "a"(0) : "ebx", "ecx", "edx");
}
EOF
if "$minic" --target=x86_64-linux -S "$d/unsafe.i" \
    -o "$d/unsafe.s" >"$d/unsafe.log" 2>&1; then
    echo 'FAIL x86 unsafe ZSTD CPUID instruction suffix admitted'; exit 1
fi
cat >"$d/wrong.i" <<'EOF'
void bad(unsigned int *eax) {
    asm("cpuid" : "=a"(*eax) : "a"(0) : "ebx", "ecx");
}
EOF
if "$minic" --target=x86_64-linux -S "$d/wrong.i" \
    -o "$d/wrong.s" >"$d/wrong.log" 2>&1; then
    echo 'FAIL x86 CPUID incomplete clobber contract admitted'; exit 1
fi
echo 'MINIC_X86_LINUX_ZSTD_CPUID=PASS layouts=3 differential=1 preserve_rbx=3 negative=2'
