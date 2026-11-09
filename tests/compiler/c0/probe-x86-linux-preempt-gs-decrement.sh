#!/usr/bin/env bash
# Original Linux v6.6.143 arch/x86/include/asm/preempt.h:
# GEN_UNARY_RMWcc("decl", pcpu_hot.preempt_count, e, __percpu_arg([var])).
# True CPU GS-relative write plus architectural ZF output; never emit a plain
# C subtraction of a TLS-looking symbol or silently drop the flag result.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
typedef _Bool bool;
struct hot { int preempt_count; };
extern struct hot pcpu_hot;
bool mini_dec(void) {
    bool c;
    asm volatile("decl " "%%gs:" "%[var]"
                 "\n\t/* output condition code " "e" " */\n"
                 : [var] "+m" (pcpu_hot.preempt_count), "=@cce" (c)
                 : : "memory");
    return c;
}
EOF
sed 's/mini_dec/gnu_dec/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
#define _GNU_SOURCE
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <asm/prctl.h>
struct hot { int preempt_count; } pcpu_hot;
extern _Bool mini_dec(void), gnu_dec(void);
int main(void) {
    unsigned long gs_base = ~0UL;
    static const int initial[] = {1, 2, 0, -1, 2147483647};
    unsigned int i;
    if (syscall(SYS_arch_prctl, ARCH_GET_GS, &gs_base) < 0 || gs_base != 0)
        return 43; /* GS is not available as zero-based in this host. */
    for (i = 0; i < sizeof(initial) / sizeof(initial[0]); ++i) {
        int m, g;
        int mr, gr;
        pcpu_hot.preempt_count = initial[i];
        mr = mini_dec();
        m = pcpu_hot.preempt_count;
        pcpu_hot.preempt_count = initial[i];
        gr = gnu_dec();
        g = pcpu_hot.preempt_count;
        if (mr != gr || m != g) return 1;
    }
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'decl %gs:' "$d/mini.s" >/dev/null
grep -E 'sete |setz ' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || test "$result" -eq 43 || {
    echo "FAIL x86 GS preempt decrement GNU differential result=$result" >&2
    exit 1
}

sed 's/%%gs:/%%fs:/g' "$d/mini.i" >"$d/wrong_segment.i"
sed 's/"=@cce"/"=@ccnz"/g' "$d/mini.i" >"$d/wrong_flag.i"
sed 's/"+m"/"=m"/g' "$d/mini.i" >"$d/wrong_access.i"
sed 's/: : "memory"/: :/g' "$d/mini.i" >"$d/no_memory_clobber.i"
for shape in wrong_segment wrong_flag wrong_access no_memory_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe GS decrement admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_PREEMPT_GS_DEC=PASS GNU_x86_differential=$([ "$result" -eq 42 ] && echo 5 || echo skip) clobber=memory reject=4"
