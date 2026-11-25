#!/usr/bin/env bash
# Named manual probes; shared initialization is exact, case assertions are unchanged.
if [ "${1:-}" = "--list" ]; then
    printf '%s\n' 'probe-x86-linux-flags-clobber-source' 'probe-x86-linux-gen-endbr' 'probe-x86-linux-kaslr-mul' 'probe-x86-linux-kexec-segments' 'probe-x86-linux-mmio-explicit-al' 'probe-x86-linux-muldiv-u64' 'probe-x86-linux-preempt-call' 'probe-x86-linux-preempt-gs-decrement' 'probe-x86-linux-segment-u32-read' 'probe-x86-linux-static-key-goto' 'probe-x86-linux-via-xstore' 'probe-x86-linux-zstd-p2align'
    exit 0
fi
__minic_probe_case=${1:?probe case required; use --list}
shift
case "$__minic_probe_case" in
  probe-x86-linux-flags-clobber-source|probe-x86-linux-gen-endbr|probe-x86-linux-kaslr-mul|probe-x86-linux-kexec-segments|probe-x86-linux-mmio-explicit-al|probe-x86-linux-muldiv-u64|probe-x86-linux-preempt-call|probe-x86-linux-preempt-gs-decrement|probe-x86-linux-segment-u32-read|probe-x86-linux-static-key-goto|probe-x86-linux-via-xstore|probe-x86-linux-zstd-p2align) ;;
  *) printf "%s\n" "unknown probe: $__minic_probe_case" >&2; exit 64 ;;
esac
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
case "$__minic_probe_case" in
probe-x86-linux-flags-clobber-source)
# GNU x86 accepts "flags" as a synonym for the machine condition-code
# clobber. Source recognition must not grant arbitrary machine-asm emission.
cat >"$d/flags.i" <<'EOF'
unsigned long flags_probe(unsigned long x) {
    asm volatile("nop" : "+r"(x) : : "flags");
    return x;
}
EOF
cc -std=gnu11 -fsyntax-only "$d/flags.i"
cc -std=gnu11 -O0 -S "$d/flags.i" -o "$d/flags-gnu.s"
# Target parser must recognize the token; machine must remain fail-closed
# until an exact opcode + physical register contract exists for this form.
if "$minic" --target=x86_64-linux -S "$d/flags.i" \
    -o "$d/flags-mini.s" >"$d/flags.out" 2>"$d/flags.err"; then
    echo "FAIL arbitrary GNU flags-clobbered asm admitted by x86 backend" >&2
    exit 1
fi
if grep -F 'unsupported GNU asm register clobber for target: flags' \
    "$d/flags.err" >/dev/null; then
    echo "FAIL x86 source-only flags clobber still rejected by parser" >&2
    exit 1
fi
sed 's/"flags"/"unknown_flag_name"/g' "$d/flags.i" >"$d/bad.i"
if "$minic" --target=x86_64-linux -S "$d/bad.i" \
    -o "$d/bad.s" >"$d/bad.out" 2>"$d/bad.err"; then
    echo "FAIL foreign GNU register clobber unexpectedly admitted" >&2
    exit 1
fi
grep -F 'unsupported GNU asm register clobber for target:' "$d/bad.err" >/dev/null
echo "MINIC_X86_LINUX_FLAGS_CLOBBER=PASS GCC_source=1 parser_alias=1 arbitrary_ASM_reject=1 invalid_reject=1"
;;
probe-x86-linux-gen-endbr)
# Linux 6.6.143 arch/x86/include/asm/ibt.h gen_endbr exact expanded GNU asm:
# a nonvolatile early-clobber 32-bit output receives the IBT opcode value.
cat >"$d/mini.i" <<'EOF'
typedef unsigned int u32;
u32 mini_endbr(void) {
    u32 endbr;
    asm("mov $~0xfa1e0ff3, %[endbr]\n\t"
        "not %[endbr]\n\t"
        : [endbr] "=&r" (endbr));
    return endbr;
}
EOF
sed 's/mini_endbr/gnu_endbr/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned int mini_endbr(void), gnu_endbr(void);
int main(void) {
    unsigned int m = mini_endbr();
    unsigned int g = gnu_endbr();
    return m == g && m == 0xfa1e0ff3U ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl $0x05e1f00c, %eax' "$d/mini.s" >/dev/null
grep -F 'notl %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux gen_endbr: GNU native differential=$result" >&2
    exit 1
}

sed 's/"=\&r"/"=r"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/u32 endbr;/unsigned long endbr;/g' "$d/mini.i" >"$d/unsafe_type.i"
sed 's/"=\&r"/"=m"/g' "$d/mini.i" >"$d/unsafe_memory_output.i"
sed 's/not %\[endbr\]/neg %[endbr]/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for test_case in unsafe_constraint unsafe_type unsafe_memory_output unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$test_case.i" \
        -o "$d/$test_case.s" >"$d/$test_case.out" 2>"$d/$test_case.err"; then
        echo "FAIL unsafe x86 Linux gen_endbr variant accepted: $test_case" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_GEN_ENDBR=PASS GNU_native=1 exact_asm=1 reject=4"
;;
probe-x86-linux-kaslr-mul)
# Linux 6.6.143 arch/x86/lib/kaslr.c _ASM_MUL "%3" exact GNU operand
# roles. Test RDX:RAX high and low halves against GCC, including overflow.
cat >"$d/mini.i" <<'EOF'
void mini_mul(unsigned long x, unsigned long y,
              unsigned long *low, unsigned long *high) {
    const unsigned long mix_const = y;
    unsigned long random = x, raw;
    asm("mulq %3" : "=a" (random), "=d" (raw)
                 : "a" (random), "rm" (mix_const));
    *low = random;
    *high = raw;
}
unsigned long mini_kaslr_const(unsigned long input) {
    const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
    unsigned long raw, random = input;
    asm("mulq %3" : "=a"(random), "=d"(raw)
                  : "a"(random), "rm"(mix_const));
    return random + raw;
}
EOF
sed -e 's/mini_mul/gnu_mul/g' -e 's/mini_kaslr_const/gnu_kaslr_const/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern void mini_mul(unsigned long, unsigned long, unsigned long *, unsigned long *);
extern void gnu_mul(unsigned long, unsigned long, unsigned long *, unsigned long *);
extern unsigned long mini_kaslr_const(unsigned long), gnu_kaslr_const(unsigned long);
int main(void) {
    static const unsigned long values[] = {
        0, 1, 2, 0xffffffffffffffffUL, 0x5d6008cbf3848dd3UL,
        0x100000000UL, 0x8000000000000000UL
    };
    unsigned i, j;
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        for (j = 0; j < sizeof(values)/sizeof(values[0]); ++j) {
            unsigned long ml, mh, gl, gh;
            mini_mul(values[i], values[j], &ml, &mh);
            gnu_mul(values[i], values[j], &gl, &gh);
            if (ml != gl || mh != gh) return 1;
        }
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        if (mini_kaslr_const(values[i]) != gnu_kaslr_const(values[i])) return 2;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'mulq %r10' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux kaslr MULQ GCC differential=$result" >&2
    exit 1
}
sed 's/"=d"/"=c"/g' "$d/mini.i" >"$d/unsafe_output.i"
sed 's/"rm"/"m"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/asm("mulq %3"/asm("divq %3"/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_output unsafe_constraint unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsupported x86 kaslr MULQ shape admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_KASLR_MUL=PASS GCC_native=49 overflow_high=1 reject=3"
;;
probe-x86-linux-kexec-segments)
# Linux 6.6.143 arch/x86/include/asm/kexec.h crash_setup_regs(): GNU
# literal EAX output gets 32-bit zero-extended segment selector, stored u64.
cat >"$d/mini.i" <<'EOF'
struct regs { unsigned long ss, cs, ds, es, fs, gs; };
void mini_segments(struct regs *r) {
    asm volatile("movl %%ss, %%eax;" : "=a"(r->ss));
    asm volatile("movl %%cs, %%eax;" : "=a"(r->cs));
    asm volatile("movl %%ds, %%eax;" : "=a"(r->ds));
    asm volatile("movl %%es, %%eax;" : "=a"(r->es));
    asm volatile("movl %%fs, %%eax;" : "=a"(r->fs));
    asm volatile("movl %%gs, %%eax;" : "=a"(r->gs));
}
EOF
sed 's/mini_segments/gnu_segments/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
struct regs { unsigned long ss, cs, ds, es, fs, gs; };
extern void mini_segments(struct regs *), gnu_segments(struct regs *);
int main(void) {
    struct regs a = {~0UL,~0UL,~0UL,~0UL,~0UL,~0UL};
    struct regs b = a;
    mini_segments(&a);
    gnu_segments(&b);
    return a.ss == b.ss && a.cs == b.cs && a.ds == b.ds &&
           a.es == b.es && a.fs == b.fs && a.gs == b.gs &&
           !(a.ss & ~0xffffUL) && !(a.cs & ~0xffffUL) ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl %ss, %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux kexec EAX segment selector GNU differential=$result" >&2
    exit 1
}
sed 's/"=a"/"=r"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/unsigned long ss/unsigned int ss/' "$d/mini.i" >"$d/unsafe_width.i"
sed 's/movl %%ss, %%eax;/movq %%ss, %%rax;/' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_constraint unsafe_width unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 kexec selector snapshot admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_KEXEC_SEGMENTS=PASS GNU_native=6 high_bits_zero=1 reject=3"
;;
probe-x86-linux-mmio-explicit-al)
# Linux 6.6.143 arch/x86/pci/mmconfig_64.c uses a fixed AL output and
# one byte-sized memory operand; the missing memory clobber is intentional.
cat >"$d/mini.i" <<'EOF'
unsigned char mini_mmio(const volatile unsigned char *p) {
    unsigned char value;
    asm volatile("movb %1, %%al" : "=a"(value) : "m"(*p));
    return value;
}
EOF
sed 's/mini_mmio/gnu_mmio/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned char mini_mmio(const volatile unsigned char *);
extern unsigned char gnu_mmio(const volatile unsigned char *);
int main(void) {
    static const unsigned char values[] = {0,1,2,0x7f,0x80,0xff};
    unsigned i;
    for (i = 0; i < sizeof(values); ++i)
        if (mini_mmio(&values[i]) != gnu_mmio(&values[i])) return 1;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movzbl (%r11), %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux MMIO explicit AL GNU differential=$result" >&2
    exit 1
}
sed 's/"=a"/"=q"/g' "$d/mini.i" >"$d/unsafe_output.i"
sed 's/"m"(\*p)/"r"(*p)/g' "$d/mini.i" >"$d/unsafe_input.i"
sed 's/"movb %1, %%al"/"movw %1, %%al"/g' "$d/mini.i" >"$d/unsafe_width.i"
for shape in unsafe_output unsafe_input unsafe_width; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 MMIO explicit AL admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_MMIO_EXPLICIT_AL=PASS GCC_native=6 reject=3"
;;
probe-x86-linux-muldiv-u64)
# Linux 6.6.143 include/linux/math64.h mul_u64_u64_div_u64(): original
# unsigned 128-bit MULQ intermediate followed directly by DIVQ, RDX clobber.
cat >"$d/mini.i" <<'EOF'
typedef unsigned long long u64;
u64 mini_muldiv(u64 a, u64 mul, u64 div) {
    u64 q;
    asm("mulq %2; divq %3" : "=a"(q)
        : "a"(a), "rm"(mul), "rm"(div) : "rdx");
    return q;
}
EOF
sed 's/mini_muldiv/gnu_muldiv/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
typedef unsigned long long u64;
extern u64 mini_muldiv(u64,u64,u64), gnu_muldiv(u64,u64,u64);
int main(void) {
    static const u64 a[] = {0,1,2,0xffffffffULL,
                            0x8000000000000000ULL,0xffffffffffffffffULL};
    static const u64 mul[] = {0,1,2,0x100000000ULL,
                              0x5d6008cbf3848dd3ULL,0xffffffffffffffffULL};
    unsigned i,j;
    for (i = 0; i < sizeof(a)/sizeof(a[0]); ++i)
        for (j = 0; j < sizeof(mul)/sizeof(mul[0]); ++j) {
            /* Divisor >= multiplier mathematically bounds quotient <= a;
             * no #DE is possible in these real CPU execution probes. */
            u64 div = mul[j] ? mul[j] : 1;
            u64 m = mini_muldiv(a[i],mul[j],div);
            u64 g = gnu_muldiv(a[i],mul[j],div);
            if (m != g) return 1;
            if (div == mul[j] && m != a[i]) return 2;
            if (mini_muldiv(a[i],mul[j],~0ULL) !=
                gnu_muldiv(a[i],mul[j],~0ULL)) return 3;
        }
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'mulq %r10' "$d/mini.s" >/dev/null
grep -F 'divq %r11' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux unsigned 128/64 MULQ DIVQ GNU differential=$result" >&2
    exit 1
}
sed 's/"rdx"/"rcx"/g' "$d/mini.i" >"$d/unsafe_clobber.i"
sed 's/"rm"(div)/"r"(div)/g' "$d/mini.i" >"$d/unsafe_divisor.i"
sed 's/"mulq %2; divq %3"/"mulq %2; idivq %3"/g' "$d/mini.i" >"$d/unsafe_signed.i"
sed 's/u64 q;/unsigned int q;/' "$d/mini.i" >"$d/unsafe_width.i"
for shape in unsafe_clobber unsafe_divisor unsafe_signed unsafe_width; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe MULQ DIVQ GNU contract admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_MULDIV_U64=PASS GNU_native=72 div0_hardware=untouched reject=4"
;;
probe-x86-linux-preempt-call)
# Linux CONFIG_PREEMPT_DYNAMIC: physical RSP +r is only a liveness anchor;
# each statement must execute its exact static-call trampoline once.
cat >"$d/mini.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void mini_preempt(void) {
    asm volatile("call __SCT__preempt_schedule" : "+r"(current_stack_pointer));
}
void mini_preempt_notrace(void) {
    asm volatile("call __SCT__preempt_schedule_notrace" : "+r"(current_stack_pointer));
}
EOF
sed 's/mini_preempt/gnu_preempt/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
volatile unsigned int preempt_counter;
volatile unsigned int notrace_counter;
void __SCT__preempt_schedule(void) { preempt_counter += 3U; }
void __SCT__preempt_schedule_notrace(void) { notrace_counter += 5U; }
extern void mini_preempt(void), mini_preempt_notrace(void);
extern void gnu_preempt(void), gnu_preempt_notrace(void);
int main(void) {
    mini_preempt();
    mini_preempt_notrace();
    gnu_preempt();
    gnu_preempt_notrace();
    return preempt_counter == 6U && notrace_counter == 10U ? 42 : 1;
}
EOF
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
test "$(grep -Fc '  call __SCT__preempt_schedule' "$d/mini.s")" -eq 2
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/oracle"
set +e
"$d/oracle"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux preempt static-call result=$result" >&2
    exit 1
}

# None of these is Linux's qualified static-call ABI. Reject altered target,
# register binding, output access and GNU volatility independently.
sed 's/__SCT__preempt_schedule_notrace/__SCT__preempt_schedule_notrace_unknown/g' \
    "$d/mini.i" >"$d/unknown.i"
sed 's/current_stack_pointer asm("rsp")/current_stack_pointer asm("r12")/g' \
    "$d/mini.i" >"$d/wrong_register.i"
sed 's/"+r"(current_stack_pointer)/"=r"(current_stack_pointer)/g' \
    "$d/mini.i" >"$d/wrong_constraint.i"
sed 's/asm volatile(/asm(/g' "$d/mini.i" >"$d/not_volatile.i"
for shape in unknown wrong_register wrong_constraint not_volatile; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL preempt static-call malformed GNU shape accepted: $shape" >&2
        exit 1
    fi
done
echo 'MINIC_X86_LINUX_PREEMPT_STATIC_CALL=PASS schedule=1 notrace=1 fixed_rsp=1 GCC_native=2 reject=4'
;;
probe-x86-linux-preempt-gs-decrement)
# Original Linux v6.6.143 arch/x86/include/asm/preempt.h:
# GEN_UNARY_RMWcc("decl", pcpu_hot.preempt_count, e, __percpu_arg([var])).
# True CPU GS-relative write plus architectural ZF output; never emit a plain
# C subtraction of a TLS-looking symbol or silently drop the flag result.
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
;;
probe-x86-linux-segment-u32-read)
# Real Linux 6.6.143 arch/x86/kernel/signal_32.c: an unsigned-int
# selector snapshot via bare mov %%gs,%0 / %%fs,%0 with memory ordering.
cat >"$d/mini.i" <<'EOF'
unsigned int mini_gs(void) {
    unsigned int v;
    asm("mov %%gs,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_fs(void) {
    unsigned int v;
    asm("mov %%fs,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_ds(void) {
    unsigned int v;
    asm("mov %%ds,%0" : "=r"(v) : : "memory");
    return v;
}
unsigned int mini_es(void) {
    unsigned int v;
    asm("mov %%es,%0" : "=r"(v) : : "memory");
    return v;
}
EOF
sed 's/mini_/gnu_/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern unsigned int mini_gs(void), mini_fs(void), mini_ds(void), mini_es(void);
extern unsigned int gnu_gs(void), gnu_fs(void), gnu_ds(void), gnu_es(void);
int main(void) {
    return mini_gs() == gnu_gs() && mini_fs() == gnu_fs() &&
           mini_ds() == gnu_ds() && mini_es() == gnu_es() &&
           (mini_gs() & ~0xffffU) == 0 ? 42 : 1;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F 'movl %gs, %eax' "$d/mini.s" >/dev/null
grep -F 'movl %fs, %eax' "$d/mini.s" >/dev/null
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux u32 segment selector GCC differential=$result" >&2
    exit 1
}
sed 's/unsigned int v;/unsigned long v;/g' "$d/mini.i" >"$d/unsafe_width.i"
sed 's/"=r"/"=m"/g' "$d/mini.i" >"$d/unsafe_constraint.i"
sed 's/ : : "memory"/ : : "rax"/g' "$d/mini.i" >"$d/unsafe_clobber.i"
for shape in unsafe_width unsafe_constraint unsafe_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 segment selector read admitted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_SEGMENT_U32_READ=PASS GCC_native=4 no_upper_garbage=1 reject=3"
;;
probe-x86-linux-static-key-goto)
# Exact Linux 6.6 arch_static_branch/arch_static_branch_jump GNU asm-goto:
# patch-site JMP and addressable __jump_table ELF metadata use a symbolic
# "i" static key, a compile-time branch bit and a C destination label.
cat >"$d/branch.i" <<'EOF'
typedef _Bool bool;
enum { false = 0, true = 1 };
struct static_key { unsigned long enabled; };
extern struct static_key key_alpha, key_beta;
static inline __attribute__((always_inline)) bool
arch_static_branch(struct static_key *key, bool branch) {
    asm goto("1:"
        "jmp %l[l_yes] # objtool NOPs this \n\t"
        ".pushsection __jump_table,  \"aw\" \n\t" " " ".balign 8" " " "\n\t"
        ".long 1b - . \n\t" ".long %l[l_yes] - . \n\t" " "
        ".quad" " " "%c0 + %c1 - .\n\t" ".popsection \n\t"
        : : "i"(key), "i"(2 | branch) : : l_yes);
    return 0;
l_yes:
    return 1;
}
static inline __attribute__((always_inline)) bool
arch_static_branch_jump(struct static_key *key, bool branch) {
    asm goto("1:"
        "jmp %l[l_yes]\n\t"
        ".pushsection __jump_table,  \"aw\" \n\t" " " ".balign 8" " " "\n\t"
        ".long 1b - . \n\t" ".long %l[l_yes] - . \n\t" " "
        ".quad" " " "%c0 + %c1 - .\n\t" ".popsection \n\t"
        : : "i"(key), "i"(branch) : : l_yes);
    return 0;
l_yes:
    return 1;
}
static inline __attribute__((always_inline)) bool
static_branch_wrapper(struct static_key *key) {
    return arch_static_branch(key, 1);
}
static inline __attribute__((always_inline)) bool
static_key_false(struct static_key *key) {
    return arch_static_branch(key, false);
}
static inline __attribute__((always_inline)) bool
static_key_true(struct static_key *key) {
    return !arch_static_branch(key, true);
}
bool mini_static_key(void) {
    return static_branch_wrapper(&key_alpha) &&
           arch_static_branch_jump(&key_beta, 0) &&
           static_key_false(&key_alpha) &&
           static_key_true(&key_beta);
}
EOF
cc -std=gnu11 -O2 -S "$d/branch.i" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/branch.i" -o "$d/mini.s"
test "$(grep -Fc '.pushsection __jump_table' "$d/mini.s")" -eq 4
cc -c "$d/mini.s" -o "$d/mini.o"
test "$(readelf -S "$d/mini.o" | grep -Fc '__jump_table')" -ge 1
test "$(readelf -Wr "$d/mini.o" | grep -Ec 'R_X86_64_PC64.*key_(alpha|beta)')" -eq 4
readelf -Wr "$d/mini.o" >"$d/relocations.txt"
grep -Eq "Relocation section '.rela__jump_table'.*contains 12 entries:" \
    "$d/relocations.txt"
# The key address and the branch bit are both ELF immediate relocations.
# Two calls to the same key with opposite branch defaults must retain both
# addends rather than accidentally reusing a shared CALL's first clone.
grep -Eq 'R_X86_64_PC64.*key_alpha \+ 2([[:space:]]|$)' "$d/relocations.txt"
grep -Eq 'R_X86_64_PC64.*key_alpha \+ 3([[:space:]]|$)' "$d/relocations.txt"
grep -Eq 'R_X86_64_PC64.*key_beta \+ 3([[:space:]]|$)' "$d/relocations.txt"

# A dynamic pointer is not a GNU `i` immediate, even if the branch is a
# literal. This rejects the unspecialized helper and prevents silently
# reinterpreting the operand as a runtime register or wrong relocation.
cat >"$d/dynamic-negative.i" <<'EOF'
typedef _Bool bool;
enum { false = 0, true = 1 };
struct static_key { unsigned long enabled; };
static inline __attribute__((always_inline)) bool
arch_static_branch(struct static_key *key, bool branch) {
    asm goto("1: jmp %l[l_yes]\n\t"
             ".pushsection __jump_table, \"aw\"\n\t"
             ".balign 8\n\t"
             ".long 1b - .\n\t"
             ".long %l[l_yes] - .\n\t"
             ".quad %c0 + %c1 - .\n\t"
             ".popsection\n\t"
             : : "i"(key), "i"(2 | branch) : : l_yes);
    return false;
l_yes:
    return true;
}
bool mini_dynamic_key(struct static_key *key) {
    return arch_static_branch(key, false);
}
EOF
if cc -std=gnu11 -O2 -S "$d/dynamic-negative.i" -o "$d/gnu-negative.s" \
        >"$d/gnu-negative.log" 2>&1; then
    echo 'MINIC_X86_STATIC_KEY_DYNAMIC_GNU_UNEXPECTED_ACCEPT' >&2
    exit 1
fi
if "$minic" --target=x86_64-linux -S "$d/dynamic-negative.i" \
        -o "$d/mini-negative.s" >"$d/mini-negative.log" 2>&1; then
    echo 'MINIC_X86_STATIC_KEY_DYNAMIC_UNEXPECTED_ACCEPT' >&2
    exit 1
fi
if ! grep -Eq 'CORE_ASM_GOTO_IMMEDIATE_FAILURE|Core IR does not yet support' \
        "$d/mini-negative.log"; then
    tail -n 12 "$d/mini-negative.log" >&2
    exit 1
fi
echo 'MINIC_X86_LINUX_STATIC_KEY_GOTO=PASS variants=4 symbolic_keys=2 real_elf=1 dynamic_immediate_rejected=1'
;;
probe-x86-linux-via-xstore)
# Linux drivers/char/hw_random/via-rng.c exact XSTORE inline ASM: a hardware
# 4-output operation with memory alias, EAX count, EDX control, EDI address.
# Do not execute the CPU opcode: physical VIA PadLock is not on every runner.
cat >"$d/mini.i" <<'EOF'
typedef unsigned int u32;
void mini_xstore(u32 *addr, u32 edx_in, u32 *eax_ptr,
                 u32 *edx_ptr, u32 **edi_ptr) {
    u32 eax_out;
    asm(".byte 0x0F,0xA7,0xC0 /* xstore %%edi (addr=%0) */"
        : "=m" (*addr), "=a" (eax_out), "+d" (edx_in), "+D" (addr));
    *eax_ptr = eax_out;
    *edx_ptr = edx_in;
    *edi_ptr = addr;
}
EOF
sed 's/mini_xstore/gnu_xstore/g' "$d/mini.i" >"$d/gnu.c"
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F '.byte 0x0f, 0xa7, 0xc0' "$d/mini.s" >/dev/null
grep -F 'movq %rdi, %r10' "$d/mini.s" >/dev/null
grep -F 'movl %edx, (%rdi)' "$d/mini.s" >/dev/null
grep -F 'movq %r10, (%rdi)' "$d/mini.s" >/dev/null
cc -c -x assembler "$d/gnu.s" -o "$d/gnu.o"
cc -c -x assembler "$d/mini.s" -o "$d/mini.o"
test "$(objdump -d "$d/gnu.o" | grep -ic '0f a7 c0')" -eq 1
test "$(objdump -d "$d/mini.o" | grep -ic '0f a7 c0')" -eq 1

# A memory-output alias differing from the hardware EDI pointer is unsafe.
sed 's/u32 eax_out;/u32 eax_out, other;/' "$d/mini.i" |
    sed 's/"=m" (\*addr)/"=m" (other)/' >"$d/unsafe_alias.i"
sed 's/u32 edx_in/unsigned long edx_in/g' "$d/mini.i" >"$d/unsafe_control.i"
sed 's/"=a"/"=r"/g' "$d/mini.i" >"$d/unsafe_register.i"
sed 's/0xA7,0xC0/0xA7,0xC1/g' "$d/mini.i" >"$d/unsafe_opcode.i"
for shape in unsafe_alias unsafe_control unsafe_register unsafe_opcode; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe VIA XSTORE variant accepted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_VIA_XSTORE=PASS GNU_AS=1 MiniC_AS=1 byte_equal=1 reject=4"
;;
probe-x86-linux-zstd-p2align)
# Linux 6.6.143 lib/zstd/decompress/zstd_decompress_block.c:
# GNU basic asm (no colon fields) is implicitly volatile and aligns an
# inner decoder loop to 64 bytes; no arbitrary opaque assembler is allowed.
# Linux's ZSTD decoder uses this precise sequence of four alignments and
# explicitly inserted `nop` instructions; basic GNU asm is volatile.
cat >"$d/mini.i" <<'EOF'
int mini_align(int x) {
    x += 1;
    asm(".p2align 6");
    x += 2;
    return x;
}

int mini_align_profile(int x) {
    x *= 2;
    asm(".p2align 6");
    asm("nop");
    asm(".p2align 5");
    asm("nop");
    asm(".p2align 4");
    asm("nop");
    asm(".p2align 3");
    return x + 3;
}
EOF
sed 's/mini_align/gnu_align/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern int mini_align(int), gnu_align(int);
extern int mini_align_profile(int), gnu_align_profile(int);
int main(void) {
    int i;
    for (i = -10; i <= 10; ++i)
        if (mini_align(i) != gnu_align(i) ||
            mini_align_profile(i) != gnu_align_profile(i)) return 1;
    return 42;
}
EOF
cc -std=gnu11 -O0 -S "$d/gnu.c" -o "$d/gnu.s"
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
grep -F '.p2align 6' "$d/mini.s" >/dev/null
for shape in '.p2align 3' '.p2align 4' '.p2align 5' 'nop'; do
    grep -F "$shape" "$d/mini.s" >/dev/null
done
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/diff"
set +e
"$d/diff"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux ZSTD align GNU differential=$result" >&2
    exit 1
}

sed 's/\.p2align 6/\.p2align 7/g' "$d/mini.i" >"$d/unsafe_alignment.i"
sed 's/\.p2align 6/\.p2align 6\\n\\tret/g' "$d/mini.i" >"$d/unsafe_payload.i"
sed 's/asm(".p2align 6")/asm(".p2align 6" : : : "memory")/g' \
    "$d/mini.i" >"$d/unsafe_extended_clobber.i"
sed 's/asm("nop")/asm("nop; ret")/g' "$d/mini.i" >"$d/unsafe_nop_payload.i"
sed 's/asm("nop")/asm("nop" : : : "memory")/g' "$d/mini.i" >"$d/unsafe_nop_clobber.i"
for shape in unsafe_alignment unsafe_payload unsafe_extended_clobber \
    unsafe_nop_payload unsafe_nop_clobber; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL unsafe x86 ZSTD alignment accepted: $shape" >&2
        exit 1
    fi
done
echo "MINIC_X86_LINUX_ZSTD_P2ALIGN=PASS GNU_native=21 align=3,4,5,6 nop=3 reject=5"
;;
esac
