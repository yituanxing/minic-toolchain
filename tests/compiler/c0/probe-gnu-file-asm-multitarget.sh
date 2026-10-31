#!/usr/bin/env bash
# One GNU basic file-scope asm source must survive all three existing
# native GNU object emitters without rewriting target ISA text.
set -Eeuo pipefail
test "$#" -eq 2
: "${MINIC_CC:?set MINIC_CC to the strict compiler}"
target=$1
cross_cc=$2
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/original.i" <<'SRC'
__asm__(".pushsection .rodata\n"
        ".globl minic_original_source_asm_marker\n"
        "minic_original_source_asm_marker:\n"
        ".byte 42\n"
        ".popsection\n");
int minic_source_asm_entry(void) { return 42; }
extern int minic_source_alias(void) __attribute__((alias("minic_source_asm_entry")));
extern int minic_source_weak_alias(void)
    __attribute__((weak, alias("minic_source_asm_entry")));
int minic_source_global_value = 42;
extern int minic_source_global_alias
    __attribute__((alias("minic_source_global_value")));
SRC
"$cross_cc" -c -x c "$work/original.i" -o "$work/original.gnu.o"
"$MINIC_CC" --target="$target-linux" -S "$work/original.i" -o "$work/original.minic.s"
test -s "$work/original.minic.s"
grep -Fq '.globl minic_original_source_asm_marker' "$work/original.minic.s"
grep -Fq 'minic_original_source_asm_marker:' "$work/original.minic.s"
grep -Fq '.byte 42' "$work/original.minic.s"
grep -Fq '.set minic_source_alias, minic_source_asm_entry' "$work/original.minic.s"
grep -Fq '.set minic_source_weak_alias, minic_source_asm_entry' "$work/original.minic.s"
grep -Fq '.weak minic_source_weak_alias' "$work/original.minic.s"
grep -Fq '.set minic_source_global_alias, minic_source_global_value' "$work/original.minic.s"
marker=$(grep -n -F 'minic_original_source_asm_marker:' "$work/original.minic.s" | head -1 | cut -d: -f1)
entry=$(grep -n -F 'minic_source_asm_entry:' "$work/original.minic.s" | head -1 | cut -d: -f1)
test "$marker" -lt "$entry"
"$cross_cc" -c -x assembler "$work/original.minic.s" -o "$work/original.minic.o"
# -W matters: default readelf truncates long ELF symbol names.
for name in original.gnu original.minic; do
    readelf -Ws "$work/$name.o" | grep -Fq 'minic_original_source_asm_marker'
    readelf -Ws "$work/$name.o" | grep -Fq 'minic_source_asm_entry'
    readelf -Ws "$work/$name.o" | grep -Fq 'minic_source_alias'
    readelf -Ws "$work/$name.o" | grep -Fq 'minic_source_weak_alias'
    readelf -Ws "$work/$name.o" | grep -Fq 'minic_source_global_alias'
done
# The relaxed word-register alias contract belongs only to LoongArch64,
# never to x86-64 SysV or AAPCS64. All mismatched cross-family machine
# function aliases must fail closed on these targets.
if [ "$target" != loongarch64 ]; then
    cat > "$work/word-register-mismatch.i" <<'SRC'
extern long sys_entry(unsigned int id, const char *ptr)
    __attribute__((alias("__se_sys_entry")));
long __se_sys_entry(long id, long ptr) { return id + (ptr != 0); }
SRC
    if "$MINIC_CC" --target="$target-linux" -S "$work/word-register-mismatch.i" \
        -o "$work/word-register-mismatch.s" >"$work/word-register-mismatch.out" \
        2>"$work/word-register-mismatch.err"; then
        echo "FAIL unexpected $target wide alias ABI admission" >&2
        exit 1
    fi
    grep -Fq 'GNU function alias requires' "$work/word-register-mismatch.err"
    echo "MINIC_ELF_ALIAS_ABI_OTHER_TARGET_REJECT=PASS target=$target"
fi

# Original Linux get_current() GNU percpu_stable_op(8): nonvolatile p
# relocation and real x86 GS segment read. Explicit GS base=0 supplies a
# safe native process oracle without faking a privileged kernel segment.
if [ "$target" = x86_64 ]; then
    cat > "$work/x86-gs-pcpu.i" <<'SRC'
extern long syscall(long, ...);
unsigned long long fake_pcpu = 42;
unsigned long long gs_read_stable(void) {
    unsigned long long result;
    __asm__("movq %%gs:%P[var], %[val]"
            : [val] "=r"(result) : [var] "p"(&fake_pcpu));
    return result;
}
int main(void) {
    if (syscall(158, 0x1001, 0L) != 0) return 2;
    return gs_read_stable() == 42 ? 0 : 1;
}
SRC
    "$cross_cc" -fno-pie -no-pie -x c "$work/x86-gs-pcpu.i" \
        -o "$work/x86-gs-pcpu.gnu.elf"
    "$MINIC_CC" --target=x86_64-linux -S "$work/x86-gs-pcpu.i" \
        -o "$work/x86-gs-pcpu.minic.s"
    grep -Fq 'movq %gs:(%r10), %rax' "$work/x86-gs-pcpu.minic.s"
    "$cross_cc" -fno-pie -no-pie -x assembler "$work/x86-gs-pcpu.minic.s" \
        -o "$work/x86-gs-pcpu.minic.elf"
    for flavor in gnu minic; do
        "$work/x86-gs-pcpu.$flavor.elf"
        objdump -d "$work/x86-gs-pcpu.$flavor.elf" |
            grep -Fq '%gs:'
    done
    for invalid in constraint suffix output-type; do
        case "$invalid" in
            constraint) asm='__asm__("movq %%gs:%P[var], %[val]" : [val] "=r"(v) : [var] "r"(&fake_pcpu));';;
            suffix) asm='__asm__("movq %%gs:%P[var], %[val]; syscall" : [val] "=r"(v) : [var] "p"(&fake_pcpu));';;
            output-type) asm='__asm__("movq %%gs:%P[var], %[val]" : [val] "=r"(v) : [var] "p"(&fake_pcpu));';;
        esac
        if [ "$invalid" = output-type ]; then vtype='unsigned int'; else vtype='unsigned long long'; fi
        printf 'unsigned long long fake_pcpu=42; int main(void){ %s v=0; %s return (int)v; }\n' \
            "$vtype" "$asm" >"$work/x86-gs-invalid-$invalid.i"
        if "$MINIC_CC" --target=x86_64-linux -S "$work/x86-gs-invalid-$invalid.i" \
            -o "$work/x86-gs-invalid-$invalid.s" \
            >"$work/x86-gs-invalid-$invalid.out" \
            2>"$work/x86-gs-invalid-$invalid.err"; then
            echo "FAIL unsafe x86 GS percpu accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_X86_LINUX_PERCPU_GS_GNU_NATIVE=PASS stable64=1 negative=3'
fi

if [ "$target" = aarch64 ]; then
    cat > "$work/aarch64-fence.i" <<'SRC'
static void full_barrier(void) {
    __asm__ __volatile__("dmb ish" : : : "memory");
    __asm__ __volatile__("dsb sy" : : : "memory");
    __asm__ __volatile__("isb" : : : "memory");
}
int main(void) { full_barrier(); return 0; }
SRC
    "$cross_cc" -x c "$work/aarch64-fence.i" -o "$work/aarch64-fence.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-fence.i" -o "$work/aarch64-fence.minic.s"
    grep -Fq 'dmb ish' "$work/aarch64-fence.minic.s"
    grep -Fq 'dsb sy' "$work/aarch64-fence.minic.s"
    grep -Fq 'isb' "$work/aarch64-fence.minic.s"
    "$cross_cc" -x assembler "$work/aarch64-fence.minic.s" -o "$work/aarch64-fence.minic.elf"
    qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-fence.gnu.elf"
    qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-fence.minic.elf"
    for unsafe in 'dmb ish; svc #0' 'dmb ishld\nsvc #0' 'dmb foo'; do
        printf 'void invalid(void){ __asm__ __volatile__("%s" : : : "memory"); }\n' \
            "$unsafe" > "$work/unsafe-barrier.i"
        if "$MINIC_CC" --target=aarch64-linux -S "$work/unsafe-barrier.i" \
            -o "$work/unsafe-barrier.s" >"$work/unsafe-barrier.out" \
            2>"$work/unsafe-barrier.err"; then
            echo 'FAIL AArch64 unqualified hardware fence accepted' >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_HARDWARE_FENCES_GNU_QEMU=PASS'

    # Original arm64 BUG() emits brk %0 with AArch64 GNU 'I'(0x401).
    # Assemble both implementations and compare the actual BRK immediate;
    # the guarded branch is never taken by QEMU-user (BRK is a trap).
    cat > "$work/aarch64-brk.i" <<'SRC'
int break_if(int enabled) {
    if (enabled) __asm__ __volatile__("brk %0" : : "I"(0x401));
    return 42;
}
int main(void) { return break_if(0) == 42 ? 0 : 1; }
SRC
    "$cross_cc" -x c "$work/aarch64-brk.i" -o "$work/aarch64-brk.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-brk.i" \
        -o "$work/aarch64-brk.minic.s"
    grep -Fq 'brk #1025' "$work/aarch64-brk.minic.s"
    "$cross_cc" -x assembler "$work/aarch64-brk.minic.s" \
        -o "$work/aarch64-brk.minic.elf"
    for flavor in gnu minic; do
        aarch64-linux-gnu-objdump -d "$work/aarch64-brk.$flavor.elf" |
            grep -Eq 'brk[[:space:]]+#0x401'
        qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-brk.$flavor.elf"
    done
    for invalid in suffix wrong_constraint shifted nonconstant; do
        case "$invalid" in
            suffix) asm='__asm__ __volatile__("brk %0; svc #0" : : "I"(1025));' ;;
            wrong_constraint) asm='__asm__ __volatile__("brk %0" : : "r"(1025));' ;;
            shifted) asm='__asm__ __volatile__("brk %0" : : "I"(4096));' ;;
            nonconstant) asm='__asm__ __volatile__("brk %0" : : "I"(n));' ;;
        esac
        printf 'int main(int n) { %s return 0; }\n' "$asm" \
            >"$work/aarch64-brk-invalid-$invalid.i"
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-brk-invalid-$invalid.i" \
            -o "$work/aarch64-brk-invalid-$invalid.s" \
            >"$work/aarch64-brk-invalid-$invalid.out" \
            2>"$work/aarch64-brk-invalid-$invalid.err"; then
            echo "FAIL unsafe AArch64 BRK template admitted: $invalid" >&2
            exit 1
        fi
        test ! -e "$work/aarch64-brk-invalid-$invalid.s"
    done
    echo 'MINIC_AARCH64_LINUX_BRK_GNU_AS_QEMU=PASS imm=0x401 negative=4'

    # Real Linux 6.6 irqflags.h DAIF privileged MRS/MSR hardware boundary.
    # Assemble and inspect objects; never execute privileged ops in QEMU-user.
    cat > "$work/aarch64-daif.i" <<'SRC'
unsigned long linux_read_daif(void) {
    unsigned long flags;
    __asm__ __volatile__("mrs %0, daif" : "=r"(flags));
    return flags;
}
void linux_restore_daif(unsigned long flags) {
    __asm__ __volatile__("msr daif, %x0" : : "rZ"(flags));
}
void linux_restore_daif_real_linux_u64(unsigned long long value) {
    __asm__ __volatile__("msr daif, %x0" : : "rZ"(value));
}
int main(void) { return 0; }
SRC
    "$cross_cc" -c -x c "$work/aarch64-daif.i" -o "$work/aarch64-daif.gnu.o"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-daif.i" \
        -o "$work/aarch64-daif.minic.s"
    grep -Fq 'mrs x9, daif' "$work/aarch64-daif.minic.s"
    grep -Fq 'msr daif, x9' "$work/aarch64-daif.minic.s"
    "$cross_cc" -c -x assembler "$work/aarch64-daif.minic.s" \
        -o "$work/aarch64-daif.minic.o"
    for flavor in gnu minic; do
        aarch64-linux-gnu-objdump -d "$work/aarch64-daif.$flavor.o" |
            grep -Eq 'mrs[[:space:]].*daif'
        aarch64-linux-gnu-objdump -d "$work/aarch64-daif.$flavor.o" |
            grep -Eq 'msr[[:space:]]+daif'
    done
    for invalid in wrong_input hidden_svc wrong_reg pointer_input wrong_width; do
        case "$invalid" in
          wrong_input) asm='__asm__ __volatile__("msr daif, %x0" : : "r"(v));' ;;
          hidden_svc) asm='__asm__ __volatile__("msr daif, %x0; svc #0" : : "rZ"(v));' ;;
          wrong_reg) asm='__asm__ __volatile__("msr spsel, %x0" : : "rZ"(v));' ;;
          pointer_input) asm='__asm__ __volatile__("msr daif, %x0" : : "rZ"(&v));' ;;
          wrong_width) asm='__asm__ __volatile__("msr daif, %x0" : : "rZ"((unsigned int)v));' ;;
        esac
        printf 'int main(void) { unsigned long v = 0; %s return 0; }\n' \
            "$asm" > "$work/aarch64-daif-invalid-$invalid.i"
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-daif-invalid-$invalid.i" \
            -o "$work/aarch64-daif-invalid-$invalid.s" \
            >"$work/aarch64-daif-invalid-$invalid.out" \
            2>"$work/aarch64-daif-invalid-$invalid.err"; then
            echo "FAIL unsafe arm64 DAIF system register accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_DAIF_GNU_AS=PASS operations=3 negative=5 original_u64=1 privileged_only=1'

    # Original Linux 6.6 arm64 __smp_store_release, all four hardware widths.
    cat > "$work/aarch64-stlr.i" <<'SRC'
typedef unsigned char u8; typedef unsigned short u16;
typedef unsigned int u32; typedef unsigned long u64;
void release1(u8 *p,u8 v){ __asm__ __volatile__("stlrb %w1, %0" : "=Q"(*p) : "rZ"(v) : "memory"); }
void release2(u16 *p,u16 v){ __asm__ __volatile__("stlrh %w1, %0" : "=Q"(*p) : "rZ"(v) : "memory"); }
void release4(u32 *p,u32 v){ __asm__ __volatile__("stlr %w1, %0" : "=Q"(*p) : "rZ"(v) : "memory"); }
void release8(u64 *p,u64 v){ __asm__ __volatile__("stlr %x1, %0" : "=Q"(*p) : "rZ"(v) : "memory"); }
int main(void){
 u8 a=0;u16 b=0;u32 c=0;u64 d=0;
 release1(&a,42);release2(&b,4242);release4(&c,424242);release8(&d,42424242);
 return a==42&&b==4242&&c==424242&&d==42424242?0:1;
}
SRC
    "$cross_cc" -x c "$work/aarch64-stlr.i" -o "$work/aarch64-stlr.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-stlr.i" \
        -o "$work/aarch64-stlr.minic.s"
    for opcode in 'stlrb w9, [x10]' 'stlrh w9, [x10]' 'stlr w9, [x10]' 'stlr x9, [x10]'; do
        grep -Fq "$opcode" "$work/aarch64-stlr.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-stlr.minic.s" \
        -o "$work/aarch64-stlr.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-stlr.$flavor.elf"
    done
    for invalid in constraint suffix width output_count; do
        case "$invalid" in
            constraint) asm='__asm__ __volatile__("stlrb %w1, %0" : "=Q"(*p) : "r"(v) : "memory");';;
            suffix) asm='__asm__ __volatile__("stlrb %w1, %0; svc #0" : "=Q"(*p) : "rZ"(v) : "memory");';;
            width) asm='__asm__ __volatile__("stlrh %w1, %0" : "=Q"(*p) : "rZ"(v) : "memory");';;
            output_count) asm='__asm__ __volatile__("stlrb %w2, %0" : "=Q"(*p), "=r"(v) : "rZ"(v) : "memory");';;
        esac
        printf 'void bad(unsigned char *p,unsigned char v) { %s }\n' "$asm" \
            >"$work/aarch64-stlr-neg-$invalid.i"
        if "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-stlr-neg-$invalid.i" \
            -o "$work/aarch64-stlr-neg-$invalid.s" \
            >"$work/aarch64-stlr-neg-$invalid.out" \
            2>"$work/aarch64-stlr-neg-$invalid.err"; then
            echo "FAIL unsafe AArch64 STLR admitted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_STLR_GNU_AS_QEMU=PASS widths=4 negative=4'

    # Exact original arm64 6.6 atomic_ll_sc.h AND/OR/XOR/ANDNOT, 32 and 64-bit.
    # LLVM/AS-style raw passthrough is forbidden: target must consume the
    # canonical existing 4 Core register/memory/scalar roles.
    : > "$work/aarch64-llsc.i"
    for width in 32 64; do
        if [ "$width" = 32 ]; then
            typename='unsigned int'; prefix=''; word0='%w0'; word3='%w3'
            logic_constraint=Kr
        else
            typename='unsigned long'; prefix='64'; word0='%0'; word3='%3'
            logic_constraint=Lr
        fi
        for operation in and or xor andnot add sub; do
            case "$operation" in
                and) opcode=and; input_constraint=$logic_constraint ;;
                or) opcode=orr; input_constraint=$logic_constraint ;;
                xor) opcode=eor; input_constraint=$logic_constraint ;;
                andnot) opcode=bic; input_constraint=r ;;
                add) opcode=add; input_constraint=Ir ;;
                sub) opcode=sub; input_constraint=Jr ;;
            esac
            cat >> "$work/aarch64-llsc.i" <<SRC
void atomic_${width}_${operation}($typename *p, $typename i) {
    $typename result;
    unsigned long tmp;
    __asm__ __volatile__("// atomic${prefix}_${operation}\\n"
                         " prfm pstl1strm, %2\\n"
                         "1: ldxr $word0, %2\\n"
                         " $opcode $word0, $word0, $word3\\n"
                         " stxr %w1, $word0, %2\\n"
                         " cbnz %w1, 1b"
                       : "=&r"(result), "=&r"(tmp), "+Q"(*p) : "$input_constraint"(i));
}
SRC
        done
    done
    cat >> "$work/aarch64-llsc.i" <<'SRC'
int main(void) {
    unsigned int a = 0x31;
    unsigned long b = 0x31;
    atomic_32_or(&a, 6); atomic_32_and(&a, 0x30); atomic_32_xor(&a, 5);
    atomic_32_andnot(&a, 0x10);
    atomic_32_add(&a, 5); atomic_32_sub(&a, 5);
    atomic_64_or(&b, 6); atomic_64_and(&b, 0x30); atomic_64_xor(&b, 5);
    atomic_64_andnot(&b, 0x10);
    atomic_64_add(&b, 5); atomic_64_sub(&b, 5);
    return a == 0x25 && b == 0x25 ? 0 : 1;
}
SRC
    "$cross_cc" -x c "$work/aarch64-llsc.i" -o "$work/aarch64-llsc.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-llsc.i" \
        -o "$work/aarch64-llsc.minic.s"
    for op in and orr eor bic add sub; do
        grep -Fq "  $op w9, w9, w11" "$work/aarch64-llsc.minic.s"
        grep -Fq "  $op x9, x9, x11" "$work/aarch64-llsc.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-llsc.minic.s" \
        -o "$work/aarch64-llsc.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-llsc.$flavor.elf"
    done
    for invalid in wrong_constraint hidden_svc wrong_label wrong_output bic_immediate_constraint; do
        case "$invalid" in
            wrong_constraint) ic='r'; suffix=''; label='1b'; out='=&r' ;;
            hidden_svc) ic='Lr'; suffix='; svc #0'; label='1b'; out='=&r' ;;
            wrong_label) ic='Lr'; suffix=''; label='2b'; out='=&r' ;;
            wrong_output) ic='Lr'; suffix=''; label='1b'; out='=r' ;;
            bic_immediate_constraint) ic='Lr'; suffix=''; label='1b'; out='=&r' ;;
        esac
        if [ "$invalid" = bic_immediate_constraint ]; then
            bad_operation='andnot'
            bad_opcode='bic'
        else
            bad_operation='or'
            bad_opcode='orr'
        fi
        cat > "$work/aarch64-llsc-invalid-$invalid.i" <<SRC
void bad(unsigned long *p, unsigned long i) {
  unsigned long result, tmp;
  __asm__ __volatile__("// atomic64_$bad_operation\\n"
    " prfm pstl1strm, %2\\n"
    "1: ldxr %0, %2\\n"
    " $bad_opcode %0, %0, %3\\n"
    " stxr %w1, %0, %2\\n"
    " cbnz %w1, $label$suffix"
    : "$out"(result), "=&r"(tmp), "+Q"(*p) : "$ic"(i));
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-llsc-invalid-$invalid.i" \
            -o "$work/aarch64-llsc-invalid-$invalid.s" \
            >"$work/aarch64-llsc-invalid-$invalid.out" \
            2>"$work/aarch64-llsc-invalid-$invalid.err"; then
            echo "FAIL unsafe arm64 LL/SC accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_LLSC_GNU_AS_QEMU=PASS operations=12 negatives=5 bic=2 add_sub=4'

    # Frozen upstream atomic_ll_sc.h atomic{,64}_fetch_or variants.
    # Verify real old-value return, memory update, and release/acquire/full
    # hardware ordering. Five-role shared Core contract, no ASM passthrough.
    : > "$work/aarch64-fetch-or.i"
    for width in 32 64; do
        if [ "$width" = 32 ]; then
            typename='int'; prefix=''; old='%w0'; newval='%w1'
            rhs='%w4'; ic=Kr
        else
            typename='long long'; prefix='64'; old='%0'; newval='%1'
            rhs='%4'; ic=Lr
        fi
        for mode in full relaxed acquire release; do
            case "$mode" in
                full) suffix=''; load=ldxr; store=stlxr
                      barrier=' dmb ish\n'; clobber=': "memory"' ;;
                relaxed) suffix='_relaxed'; load=ldxr; store=stxr
                         barrier=''; clobber='' ;;
                acquire) suffix='_acquire'; load=ldaxr; store=stxr
                         barrier=''; clobber=': "memory"' ;;
                release) suffix='_release'; load=ldxr; store=stlxr
                         barrier=''; clobber=': "memory"' ;;
            esac
            cat >> "$work/aarch64-fetch-or.i" <<SRC
$typename atomic_${width}_fetch_or_$mode($typename *p, $typename value) {
    $typename result, next;
    unsigned long retry;
    __asm__ __volatile__("// atomic${prefix}_fetch_or${suffix}\\n"
                         " prfm pstl1strm, %3\\n"
                         "1: $load $old, %3\\n"
                         " orr $newval, $old, $rhs\\n"
                         " $store %w2, $newval, %3\\n"
                         " cbnz %w2, 1b\\n"
                         "$barrier"
                         : "=&r"(result), "=&r"(next), "=&r"(retry),
                           "+Q"(*p) : "$ic"(value) $clobber);
    return result;
}
SRC
        done
    done
    cat >> "$work/aarch64-fetch-or.i" <<'SRC'
int main(void) {
    int a = 1;
    long long b = 1;
    int av0 = atomic_32_fetch_or_full(&a, 2);
    int av1 = atomic_32_fetch_or_relaxed(&a, 4);
    int av2 = atomic_32_fetch_or_acquire(&a, 8);
    int av3 = atomic_32_fetch_or_release(&a, 16);
    long long bv0 = atomic_64_fetch_or_full(&b, 2);
    long long bv1 = atomic_64_fetch_or_relaxed(&b, 4);
    long long bv2 = atomic_64_fetch_or_acquire(&b, 8);
    long long bv3 = atomic_64_fetch_or_release(&b, 16);
    return a == 31 && b == 31 &&
           av0 == 1 && av1 == 3 && av2 == 7 && av3 == 15 &&
           bv0 == 1 && bv1 == 3 && bv2 == 7 && bv3 == 15 ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-fetch-or.i" \
        -o "$work/aarch64-fetch-or.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-fetch-or.i" \
        -o "$work/aarch64-fetch-or.minic.s"
    for mnemonic in 'ldxr x9' 'ldaxr x9' 'stxr w12' 'stlxr w12' 'orr x13, x9, x11' 'orr w13, w9, w11' 'dmb ish'; do
        grep -Fq "$mnemonic" "$work/aarch64-fetch-or.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-fetch-or.minic.s" \
        -o "$work/aarch64-fetch-or.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-fetch-or.$flavor.elf"
    done
    for invalid in wrong_input hidden_svc changed_opcode wrong_memory wrong_clobber; do
        ic=Lr; extra=''; opcode=orr; mem='+Q'; clobber=''
        case "$invalid" in
            wrong_input) ic=r ;;
            hidden_svc) extra='; svc #0' ;;
            changed_opcode) opcode=eor ;;
            wrong_memory) mem='=Q' ;;
            wrong_clobber) clobber=': "memory"' ;;
        esac
        cat > "$work/aarch64-fetch-or-invalid-$invalid.i" <<SRC
void bad(long long *p,long long i) {
  long long old, next;
  unsigned long retry;
  __asm__ __volatile__("// atomic64_fetch_or_relaxed\\n"
    " prfm pstl1strm, %3\\n"
    "1: ldxr %0, %3\\n"
    " $opcode %1, %0, %4\\n"
    " stxr %w2, %1, %3\\n"
    " cbnz %w2, 1b$extra"
    : "=&r"(old), "=&r"(next), "=&r"(retry), "$mem"(*p)
    : "$ic"(i) $clobber);
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-fetch-or-invalid-$invalid.i" \
            -o "$work/aarch64-fetch-or-invalid-$invalid.s" \
            >"$work/aarch64-fetch-or-invalid-$invalid.out" \
            2>"$work/aarch64-fetch-or-invalid-$invalid.err"; then
            echo "FAIL invalid AArch64 GNU fetch-OR admitted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_FETCH_OR_GNU_QEMU=PASS variants=8 negative=5 old_value=1 ordering=full,relaxed,acquire,release'

    # Shared five-role LL/SC ISA, 24 more actual Linux fetch forms.
    : > "$work/aarch64-fetch-logic.i"
    for width in 32 64; do
        if [ "$width" = 32 ]; then
            ctype=int; prefix=''; old='%w0'; next='%w1'; rhs='%w4'; constraint=Kr
        else
            ctype='long long'; prefix=64; old='%0'; next='%1'; rhs='%4'; constraint=Lr
        fi
        for operation in and xor andnot add sub; do
            case "$operation" in
                and) alu=and; operand_constraint=$constraint ;;
                xor) alu=eor; operand_constraint=$constraint ;;
                andnot) alu=bic; operand_constraint=r ;;
                add) alu=add; operand_constraint=Ir ;;
                sub) alu=sub; operand_constraint=Jr ;;
            esac
            for mode in full relaxed acquire release; do
                case "$mode" in
                    full) suff=''; load=ldxr; store=stlxr
                          barrier=' dmb ish\n'; clobber=': "memory"' ;;
                    relaxed) suff=_relaxed; load=ldxr; store=stxr
                             barrier=''; clobber='' ;;
                    acquire) suff=_acquire; load=ldaxr; store=stxr
                             barrier=''; clobber=': "memory"' ;;
                    release) suff=_release; load=ldxr; store=stlxr
                             barrier=''; clobber=': "memory"' ;;
                esac
                name=$(printf 'fetch_%s_%s_%s' "$width" "$operation" "$mode")
                tag=$(printf 'atomic%s_fetch_%s%s' "$prefix" "$operation" "$suff")
                cat >> "$work/aarch64-fetch-logic.i" <<SRC
$ctype $name($ctype *p, $ctype v) {
    $ctype old_value, new_value;
    unsigned long retry;
    __asm__ __volatile__("// $tag\\n"
                         " prfm pstl1strm, %3\\n"
                         "1: $load $old, %3\\n"
                         " $alu $next, $old, $rhs\\n"
                         " $store %w2, $next, %3\\n"
                         " cbnz %w2, 1b\\n"
                         "$barrier"
                         : "=&r"(old_value), "=&r"(new_value), "=&r"(retry),
                           "+Q"(*p) : "$operand_constraint"(v) $clobber);
    return old_value;
}
SRC
            done
            if [ "$operation" = and ]; then
                init=63; a=62; b=61; c=59; d=55
                x=63; y=62; z=60; w=56; endval=48
            elif [ "$operation" = xor ]; then
                init=0; a=1; b=2; c=4; d=8
                x=0; y=1; z=3; w=7; endval=15
            elif [ "$operation" = add ]; then
                init=1; a=2; b=3; c=4; d=5
                x=1; y=3; z=6; w=10; endval=15
            elif [ "$operation" = sub ]; then
                init=32; a=1; b=2; c=4; d=8
                x=32; y=31; z=29; w=25; endval=17
            else
                init=63; a=1; b=2; c=4; d=8
                x=63; y=62; z=60; w=56; endval=48
            fi
            testname=$(printf 'test_%s_%s' "$width" "$operation")
            name0=$(printf 'fetch_%s_%s_full' "$width" "$operation")
            name1=$(printf 'fetch_%s_%s_relaxed' "$width" "$operation")
            name2=$(printf 'fetch_%s_%s_acquire' "$width" "$operation")
            name3=$(printf 'fetch_%s_%s_release' "$width" "$operation")
            cat >> "$work/aarch64-fetch-logic.i" <<SRC
int $testname(void) {
    $ctype v = $init;
    $ctype x0 = $name0(&v,$a);
    $ctype x1 = $name1(&v,$b);
    $ctype x2 = $name2(&v,$c);
    $ctype x3 = $name3(&v,$d);
    return v == $endval && x0 == $x && x1 == $y && x2 == $z && x3 == $w;
}
SRC
        done
    done
    cat >> "$work/aarch64-fetch-logic.i" <<'SRC'
int main(void) {
    return test_32_and() && test_32_xor() && test_32_andnot() &&
           test_32_add() && test_32_sub() &&
           test_64_and() && test_64_xor() && test_64_andnot() &&
           test_64_add() && test_64_sub() ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-fetch-logic.i" \
        -o "$work/aarch64-fetch-logic.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-fetch-logic.i" \
        -o "$work/aarch64-fetch-logic.minic.s"
    for opcode in 'and w13, w9, w11' 'and x13, x9, x11' \
                  'eor w13, w9, w11' 'eor x13, x9, x11' \
                  'bic w13, w9, w11' 'bic x13, x9, x11' \
                  'add w13, w9, w11' 'add x13, x9, x11' \
                  'sub w13, w9, w11' 'sub x13, x9, x11'; do
        grep -Fq "$opcode" "$work/aarch64-fetch-logic.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-fetch-logic.minic.s" \
        -o "$work/aarch64-fetch-logic.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-fetch-logic.$flavor.elf"
    done
    for invalid in bad_constraint opcode_injection hidden_svc wrong_memory extra_clobber; do
        c=r; opcode=bic; suffix=''; mem='+Q'; clobber=''
        case "$invalid" in
            bad_constraint) c=Lr ;;
            opcode_injection) opcode=eor ;;
            hidden_svc) suffix='; svc #0' ;;
            wrong_memory) mem='=Q' ;;
            extra_clobber) clobber=': "memory"' ;;
        esac
        cat > "$work/aarch64-fetch-logic-bad-$invalid.i" <<SRC
void bad(long long *p, long long i) {
    long long old, next;
    unsigned long retry;
    __asm__ __volatile__("// atomic64_fetch_andnot_relaxed\\n"
        " prfm pstl1strm, %3\\n"
        "1: ldxr %0, %3\\n"
        " $opcode %1, %0, %4\\n"
        " stxr %w2, %1, %3\\n"
        " cbnz %w2, 1b$suffix"
        : "=&r"(old), "=&r"(next), "=&r"(retry), "$mem"(*p)
        : "$c"(i) $clobber);
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-fetch-logic-bad-$invalid.i" \
            -o "$work/aarch64-fetch-logic-bad-$invalid.s" \
            >"$work/aarch64-fetch-logic-bad-$invalid.out" \
            2>"$work/aarch64-fetch-logic-bad-$invalid.err"; then
            echo "FAIL unqualified AArch64 fetch logical ASM: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_FETCH_LOGIC_GNU_QEMU=PASS variants=40 operations=and,xor,andnot,add,sub ordering=4 negative=5'

    # Original Linux uaccess.h __uaccess_mask_ptr: bit55 tag BIC immediate.
    # Not a plain AND lowering: require exact GNU i constant and hardware BIC.
    cat > "$work/aarch64-uaccess-bic.i" <<'SRC'
typedef unsigned long u64;
void *linux_uaccess_mask_ptr(const void *p) {
    void *safe;
    __asm__ __volatile__("\tbic\t%0, %1, %2\n"
                         : "=r"(safe) : "r"(p), "i"(((1UL) << (55))));
    return safe;
}
int main(void) {
    u64 p = (1UL << 55) | 0x1234UL;
    return (u64)linux_uaccess_mask_ptr((void *)p) == 0x1234UL ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-uaccess-bic.i" \
        -o "$work/aarch64-uaccess-bic.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-uaccess-bic.i" \
        -o "$work/aarch64-uaccess-bic.minic.s"
    grep -Fq 'bic x9, x10, #0x80000000000000' \
        "$work/aarch64-uaccess-bic.minic.s"
    "$cross_cc" -x assembler "$work/aarch64-uaccess-bic.minic.s" \
        -o "$work/aarch64-uaccess-bic.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-uaccess-bic.$flavor.elf"
        aarch64-linux-gnu-objdump -d "$work/aarch64-uaccess-bic.$flavor.elf" |
            grep -Eq '(bic|and)[[:space:]]+x[0-9]+'
    done
    for invalid in wrong_mask register_constraint hidden_svc memory_clobber; do
        case "$invalid" in
            wrong_mask) mask='((1UL) << 54)'; input=i; instruction='bic %0, %1, %2'; clobber='' ;;
            register_constraint) mask='((1UL) << 55)'; input=r; instruction='bic %0, %1, %2'; clobber='' ;;
            hidden_svc) mask='((1UL) << 55)'; input=i; instruction='bic %0, %1, %2\\n svc #0'; clobber='' ;;
            memory_clobber) mask='((1UL) << 55)'; input=i; instruction='bic %0, %1, %2'; clobber=': "memory"' ;;
        esac
        cat > "$work/aarch64-uaccess-bic-bad-$invalid.i" <<SRC
void *bad(const void *p) {
    void *result;
    __asm__ __volatile__("$instruction" : "=r"(result) :
                          "r"(p), "$input"($mask) $clobber);
    return result;
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-uaccess-bic-bad-$invalid.i" \
            -o "$work/aarch64-uaccess-bic-bad-$invalid.s" \
            >"$work/aarch64-uaccess-bic-bad-$invalid.out" \
            2>"$work/aarch64-uaccess-bic-bad-$invalid.err"; then
            echo "FAIL AArch64 unsafe uaccess BIC accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_UACCESS_BIC_GNU_QEMU=PASS tag_bit=55 negative=4'


    # True ORIGINAL 6.6.143 arm64 per-CPU RMW source: 8/16/32/64,
    # three operations plus add_return = sixteen LL/SC hardware variants.
    # Separate GNU/GAS and MiniC/GAS QEMU execution proves real atomicity
    # and exact 1/2/4/8B access widths. No frontend ISA branch or C RMW.
    : > "$work/aarch64-percpu-llsc.i"
    for bits in 8 16 32 64; do
        case "$bits" in
            8) typ='unsigned char'; suffix=b; w='%w' ;;
            16) typ='unsigned short'; suffix=h; w='%w' ;;
            32) typ='unsigned int'; suffix=''; w='%w' ;;
            64) typ='unsigned long'; suffix=''; w='%' ;;
        esac
        for op in add andnot or add_return; do
            case "$op" in
                add|add_return) alu=add ;;
                andnot) alu=bic ;;
                or) alu=orr ;;
            esac
            if [ "$op" = add_return ]; then label=ret; else label=tmp; fi
            cat >> "$work/aarch64-percpu-llsc.i" <<SRC
$typ percpu_${bits}_${op}(void *p,$typ val) {
    unsigned int loop;
    $typ $label;
    __asm__ __volatile__(
        "1: ldxr$suffix $w[$label], %[ptr]\n"
        " $alu $w[$label], $w[$label], $w[val]\n"
        " stxr$suffix %w[loop], $w[$label], %[ptr]\n"
        " cbnz %w[loop], 1b"
        : [loop] "=&r"(loop), [$label] "=&r"($label),
          [ptr] "+Q"(*($typ *)p) : [val] "r"(val));
    return $label;
}
SRC
        done
        cat >> "$work/aarch64-percpu-llsc.i" <<SRC
int test_percpu_$bits(void) {
    $typ value=85;
    $typ a=percpu_${bits}_add(&value,3);
    $typ b=percpu_${bits}_andnot(&value,8);
    $typ c=percpu_${bits}_or(&value,3);
    $typ d=percpu_${bits}_add_return(&value,5);
    return value==88 && a==88 && b==80 && c==83 && d==88;
}
SRC
    done
    cat >> "$work/aarch64-percpu-llsc.i" <<'SRC'
int main(void) {
    return test_percpu_8() && test_percpu_16() &&
           test_percpu_32() && test_percpu_64() ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-percpu-llsc.i" \
        -o "$work/aarch64-percpu-llsc.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-percpu-llsc.i" \
        -o "$work/aarch64-percpu-llsc.minic.s"
    for opcode in 'ldxrb w9' 'ldxrh w9' 'ldxr w9' 'ldxr x9' \
                  'stxrb w12' 'stxrh w12' 'stxr w12' \
                  'add w9' 'bic w9' 'orr w9' \
                  'strb w9' 'strh w9' 'str w9' 'str x9'; do
        grep -Fq "$opcode" "$work/aarch64-percpu-llsc.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-percpu-llsc.minic.s" \
        -o "$work/aarch64-percpu-llsc.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-percpu-llsc.$flavor.elf"
    done
    for bad in injected wrong_input wrong_mem; do
        tail=''; input=r; memory='+Q'
        case "$bad" in
            injected) tail='; svc #0' ;;
            wrong_input) input=i ;;
            wrong_mem) memory='=Q' ;;
        esac
        cat > "$work/aarch64-percpu-bad-$bad.i" <<SRC
void bad(unsigned int *p,unsigned int n) {
    unsigned int loop, tmp;
    __asm__ __volatile__(
        "1: ldxr %w1, %2\n"
        " add %w1, %w1, %w3\n"
        " stxr %w0, %w1, %2\n"
        " cbnz %w0, 1b$tail"
        : "=&r"(loop), "=&r"(tmp), "$memory"(*p) : "$input"(n));
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-percpu-bad-$bad.i" \
            -o "$work/aarch64-percpu-bad-$bad.s" >/dev/null 2>&1; then
            echo "FAIL unqualified AArch64 per-CPU LL/SC accepted: $bad" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_PERCPU_RMW_GNU_QEMU=PASS variants=16 widths=4 ops=3 return=4 negative=3'

    # Original Linux 6.6.143 __kern_my_cpu_offset() from frozen GNU .i.
    # The ALTERNATIVE patch table must survive as an ELF section containing
    # TWO relocation entries. A privileged MRS cannot run in QEMU-user:
    # GNU GCC/GAS and MiniC/GAS object/relocation equivalence is the oracle.
    cat > "$work/aarch64-percpu-alternative.i" <<'SRC'
register unsigned long current_stack_pointer asm ("sp");
unsigned long linux_kern_my_cpu_offset(void) {
    unsigned long off;
    asm(".if ""1"" == 1\n" "661:\n\t"
        "mrs %0, tpidr_el1" "\n" "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n" " .word 663f - .\n"
        " .hword " "47" "\n" " .byte 662b-661b\n"
        " .byte 664f-663f\n" ".popsection\n"
        ".subsection 1\n" "663:\n\t"
        "mrs %0, tpidr_el2" "\n" "664:\n\t"
        ".org\t. - (664b-663b) + (662b-661b)\n\t"
        ".org\t. - (662b-661b) + (664b-663b)\n\t"
        ".previous\n" ".endif\n"
        : "=r" (off)
        : "Q" (*(const unsigned long *)current_stack_pointer));
    return off;
}
int main(void) { return 0; }
SRC
    "$cross_cc" -O0 -c -x c "$work/aarch64-percpu-alternative.i" \
        -o "$work/aarch64-percpu-alternative.gnu.o"
    # Scope machine-reject diagnostics to this exact original Linux
    # blocker; a previous -S success wrote a fail-closed .error directive
    # that only GNU GAS discovered. Inspect it before attempting GAS.
    MINIC_M1_TRACE=1 "$MINIC_CC" --target=aarch64-linux -S \
        "$work/aarch64-percpu-alternative.i" \
        -o "$work/aarch64-percpu-alternative.minic.s"
    if grep -Eq '^[[:space:]]*\.error[[:space:]]' \
        "$work/aarch64-percpu-alternative.minic.s"; then
        grep -E '^[[:space:]]*\.error[[:space:]]' \
            "$work/aarch64-percpu-alternative.minic.s" >&2
        echo 'FAIL authentic Linux AArch64 alternative backend rejected Core' >&2
        exit 1
    fi
    for instruction in 'mrs x9, tpidr_el1' 'mrs x9, tpidr_el2' \
                       '.pushsection .altinstructions' '.hword 47' \
                       '.org . - (664b-663b) + (662b-661b)'; do
        grep -Fq "$instruction" "$work/aarch64-percpu-alternative.minic.s"
    done
    "$cross_cc" -c -x assembler "$work/aarch64-percpu-alternative.minic.s" \
        -o "$work/aarch64-percpu-alternative.minic.o"
    for flavor in gnu minic; do
        file="$work/aarch64-percpu-alternative.$flavor.o"
        aarch64-linux-gnu-readelf -SW "$file" |
            grep -Fq '.altinstructions'
        aarch64-linux-gnu-readelf -rW "$file" |
            grep -Fq '.rela.altinstructions'
        aarch64-linux-gnu-readelf -rW "$file" |
            grep -q 'R_AARCH64_PREL32'
        aarch64-linux-gnu-objdump -d "$file" |
            grep -Eq 'mrs[[:space:]].*tpidr_el1'
        aarch64-linux-gnu-objdump -d "$file" |
            grep -Eq 'mrs[[:space:]].*tpidr_el2'
        aarch64-linux-gnu-readelf -SW "$file" |
            grep -E '[[:space:]]\.altinstructions[[:space:]]' |
            grep -q '00000c'
        [ "$(aarch64-linux-gnu-readelf -rW "$file" |
             grep -c 'R_AARCH64_PREL32')" -ge 2 ]
    done
    for bad in feature replacement input_constraint output_constraint volatile patch_injection; do
        case "$bad" in
            feature)
                sed 's/"47"/"48"/' \
                    "$work/aarch64-percpu-alternative.i" ;;
            replacement)
                sed 's/mrs %0, tpidr_el2/mrs %0, tpidr_el3/' \
                    "$work/aarch64-percpu-alternative.i" ;;
            input_constraint)
                sed 's/: "Q" (\*(const unsigned long \*)current_stack_pointer)/: "r" (*(const unsigned long *)current_stack_pointer)/' \
                    "$work/aarch64-percpu-alternative.i" ;;
            output_constraint)
                sed 's/: "=r" (off)/: "=m" (off)/' \
                    "$work/aarch64-percpu-alternative.i" ;;
            volatile)
                sed 's/    asm("\.if/    asm volatile(".if/' \
                    "$work/aarch64-percpu-alternative.i" ;;
            patch_injection)
                sed 's/\.previous\\n" "\.endif/\.previous\\n" "svc #0\\n" "\.endif/' \
                    "$work/aarch64-percpu-alternative.i" ;;
        esac > "$work/aarch64-percpu-alt-bad-$bad.i"
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-percpu-alt-bad-$bad.i" \
            -o "$work/aarch64-percpu-alt-bad-$bad.s" \
            >"$work/aarch64-percpu-alt-bad-$bad.out" \
            2>"$work/aarch64-percpu-alt-bad-$bad.err"; then
            echo "FAIL unqualified Linux AArch64 alternative ASM admitted: $bad" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_PERCPU_ALTERNATIVE_GNU_AS=PASS real_frozen=1 patchtable=12B relocations=2 feature=47 negative=6 privileged_only=1'



    # Frozen 6.6.143 atomic_ll_sc.h ADD/SUB return: both widths × four
    # memory-order variants. Prove the *new* value is returned, not old.
    : > "$work/aarch64-atomic-return.i"
    for bits in 32 64; do
        if [ "$bits" = 32 ]; then
            ctype='int'; word='%w0'; rhs='%w3'; prefix=''
        else
            ctype='long long'; word='%0'; rhs='%3'; prefix=64
        fi
        for op in add sub; do
            if [ "$op" = add ]; then
                constraint=Ir; initial=10
                values='11 13 16 20'
            else
                constraint=Jr; initial=40
                values='39 37 34 30'
            fi
            for mode in full relaxed acquire release; do
                case "$mode" in
                    full) suffix=''; ld=ldxr; st=stlxr
                          barrier=' dmb ish'; clobber=': "memory"' ;;
                    relaxed) suffix=_relaxed; ld=ldxr; st=stxr
                             barrier=''; clobber='' ;;
                    acquire) suffix=_acquire; ld=ldaxr; st=stxr
                             barrier=''; clobber=': "memory"' ;;
                    release) suffix=_release; ld=ldxr; st=stlxr
                             barrier=''; clobber=': "memory"' ;;
                esac
                name=$(printf 'atomic_ret_%s_%s_%s' "$bits" "$op" "$mode")
                tag=$(printf 'atomic%s_%s_return%s' "$prefix" "$op" "$suffix")
                cat >> "$work/aarch64-atomic-return.i" <<SRC
$ctype $name($ctype *p, $ctype n) {
    $ctype result;
    unsigned long retry;
    __asm__ __volatile__("// $tag\n"
        " prfm pstl1strm, %2\n"
        "1: $ld $word, %2\n"
        " $op $word, $word, $rhs\n"
        " $st %w1, $word, %2\n"
        " cbnz %w1, 1b\n"
        "$barrier"
        : "=&r"(result), "=&r"(retry), "+Q"(*p)
        : "$constraint"(n) $clobber);
    return result;
}
SRC
            done
            name=$(printf 'test_return_%s_%s' "$bits" "$op")
            cat >> "$work/aarch64-atomic-return.i" <<SRC
int $name(void) {
    $ctype v=$initial;
    $ctype r0=$(printf 'atomic_ret_%s_%s_full' "$bits" "$op")(&v,1);
    $ctype r1=$(printf 'atomic_ret_%s_%s_relaxed' "$bits" "$op")(&v,2);
    $ctype r2=$(printf 'atomic_ret_%s_%s_acquire' "$bits" "$op")(&v,3);
    $ctype r3=$(printf 'atomic_ret_%s_%s_release' "$bits" "$op")(&v,4);
    return v==$(echo "$values" | awk '{print $4}') &&
           r0==$(echo "$values" | awk '{print $1}') &&
           r1==$(echo "$values" | awk '{print $2}') &&
           r2==$(echo "$values" | awk '{print $3}') &&
           r3==$(echo "$values" | awk '{print $4}');
}
SRC
        done
    done
    cat >> "$work/aarch64-atomic-return.i" <<'SRC'
int main(void) {
    return test_return_32_add() && test_return_32_sub() &&
           test_return_64_add() && test_return_64_sub() ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-atomic-return.i" \
        -o "$work/aarch64-atomic-return.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-atomic-return.i" \
        -o "$work/aarch64-atomic-return.minic.s"
    for opcode in 'add w9, w9, w11' 'sub w9, w9, w11' \
                  'add x9, x9, x11' 'sub x9, x9, x11' \
                  'ldaxr w9' 'ldaxr x9' 'stlxr w12' 'str w9' 'str x9'; do
        grep -Fq "$opcode" "$work/aarch64-atomic-return.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-atomic-return.minic.s" \
        -o "$work/aarch64-atomic-return.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-atomic-return.$flavor.elf"
    done
    for bad in wrong_constraint wrong_opcode hidden_svc no_memory wrong_memory; do
        constraint=Ir; opcode=add; suffix=''; clobber=': "memory"'; memory='+Q'
        case "$bad" in
            wrong_constraint) constraint=r ;;
            wrong_opcode) opcode=sub ;;
            hidden_svc) suffix='; svc #0' ;;
            no_memory) clobber='' ;;
            wrong_memory) memory='=Q' ;;
        esac
        cat > "$work/aarch64-atomic-return-bad-$bad.i" <<SRC
int bad(int *p, int n) {
    int result; unsigned long retry;
    __asm__ __volatile__("// atomic_add_return\n"
        " prfm pstl1strm, %2\n"
        "1: ldxr %w0, %2\n"
        " $opcode %w0, %w0, %w3\n"
        " stlxr %w1, %w0, %2\n"
        " cbnz %w1, 1b\n"
        " dmb ish$suffix"
        : "=&r"(result), "=&r"(retry), "$memory"(*p)
        : "$constraint"(n) $clobber);
    return result;
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-atomic-return-bad-$bad.i" \
            -o "$work/aarch64-atomic-return-bad-$bad.s" \
            >"$work/aarch64-atomic-return-bad-$bad.out" \
            2>"$work/aarch64-atomic-return-bad-$bad.err"; then
            echo "FAIL unsafe AArch64 Linux atomic return admitted: $bad" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_ATOMIC_RETURN_GNU_QEMU=PASS variants=16 widths=2 operations=2 orderings=4 negative=5'

    # Original Linux 6.6.143 cmpxchg.h XCHG; 4 widths × 4 memory
    # orderings, old-value semantics and true GNU/GAS/QEMU differential.
    : > "$work/aarch64-xchg-kernel.i"
    for bits in 8 16 32 64; do
        case "$bits" in
            8) typ='unsigned char'; suffix=b; old='%w0'; new='%w3' ;;
            16) typ='unsigned short'; suffix=h; old='%w0'; new='%w3' ;;
            32) typ='unsigned int'; suffix=''; old='%w0'; new='%w3' ;;
            64) typ='unsigned long'; suffix=''; old='%0'; new='%3' ;;
        esac
        for mode in relaxed acquire release full; do
            case "$mode" in
                relaxed) ld=ld; st=st; fence=''; clobber='' ;;
                acquire) ld=lda; st=st; fence=''; clobber=': "memory"' ;;
                release) ld=ld; st=stl; fence=''; clobber=': "memory"' ;;
                full) ld=ld; st=stl; fence=' dmb ish'; clobber=': "memory"' ;;
            esac
            cat >> "$work/aarch64-xchg-kernel.i" <<SRC
$typ $(printf "xchg_%s_%s" "$bits" "$mode")($typ *p, $typ value) {
    $typ old_value;
    unsigned long retry;
    __asm__ __volatile__(" prfm pstl1strm, %2\n"
        "1: $(printf '%s' "$ld")xr$suffix $old, %2\n"
        " $(printf '%s' "$st")xr$suffix %w1, $new, %2\n"
        " cbnz %w1, 1b\n"
        "$fence"
        : "=&r"(old_value), "=&r"(retry), "+Q"(*p)
        : "r"(value) $clobber);
    return old_value;
}
SRC
        done
        cat >> "$work/aarch64-xchg-kernel.i" <<SRC
int test_xchg_$bits(void) {
    $typ v=1;
    $typ a=$(printf "xchg_%s_relaxed" "$bits")(&v,2);
    $typ b=$(printf "xchg_%s_acquire" "$bits")(&v,3);
    $typ c=$(printf "xchg_%s_release" "$bits")(&v,4);
    $typ d=$(printf "xchg_%s_full" "$bits")(&v,5);
    return v==5 && a==1 && b==2 && c==3 && d==4;
}
SRC
    done
    cat >> "$work/aarch64-xchg-kernel.i" <<'SRC'
int main(void) {
    return test_xchg_8() && test_xchg_16() &&
           test_xchg_32() && test_xchg_64() ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-xchg-kernel.i" \
        -o "$work/aarch64-xchg-kernel.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S \
        "$work/aarch64-xchg-kernel.i" -o "$work/aarch64-xchg-kernel.minic.s"
    for mnemonic in 'ldxrb w9' 'ldxrh w9' 'ldxr w9' 'ldxr x9' \
                    'ldaxrb w9' 'ldaxrh w9' 'ldaxr w9' 'ldaxr x9' \
                    'stlxrb w12' 'stlxrh w12' 'stlxr w12' \
                    'strb w9' 'strh w9' 'str w9' 'str x9' 'dmb ish'; do
        grep -Fq "$mnemonic" "$work/aarch64-xchg-kernel.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-xchg-kernel.minic.s" \
        -o "$work/aarch64-xchg-kernel.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-xchg-kernel.$flavor.elf"
    done
    for invalid in injection wrong_constraint missing_clobber wrong_memory extra_clobber; do
        suffix=''; constraint=r; clobber=': "memory"'; mem='+Q'
        case "$invalid" in
            injection) suffix='; svc #0' ;;
            wrong_constraint) constraint=Ir ;;
            missing_clobber) clobber='' ;;
            wrong_memory) mem='=Q' ;;
            extra_clobber) clobber=': "memory", "cc"' ;;
        esac
        cat > "$work/aarch64-xchg-invalid-$invalid.i" <<SRC
void bad(unsigned long *p, unsigned long value) {
    unsigned long old, retry;
    __asm__ __volatile__("prfm pstl1strm, %2\n"
        "1: ldxr %0, %2\n"
        " stlxr %w1, %3, %2\n"
        " cbnz %w1, 1b\n"
        " dmb ish$suffix"
        : "=&r"(old), "=&r"(retry), "$mem"(*p)
        : "$constraint"(value) $clobber);
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-xchg-invalid-$invalid.i" \
            -o "$work/aarch64-xchg-invalid-$invalid.s" \
            >"$work/aarch64-xchg-invalid-$invalid.out" \
            2>"$work/aarch64-xchg-invalid-$invalid.err"; then
            echo "FAIL unsafe AArch64 Linux XCHG accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_XCHG_GNU_QEMU=PASS variants=16 widths=4 orderings=4 negative=5'

    # Exact Linux cmpxchg 32/64 × relaxed/acquire/release/full, both
    # success and mismatch old-value behavior. Actual GNU+MiniC GAS/QEMU.
    : > "$work/aarch64-cmpxchg.i"
    for bits in 32 64; do
        if [ "$bits" = 32 ]; then
            typ='unsigned int'; oldval='%w[oldval]'; tmp='%w[tmp]'
            oldparam='%w[old]'; newparam='%w[new]'; constraint=Kr
        else
            typ='unsigned long'; oldval='%[oldval]'; tmp='%[tmp]'
            oldparam='%[old]'; newparam='%[new]'; constraint=Lr
        fi
        for mode in relaxed acquire release full; do
            case "$mode" in
                relaxed) ld=ldxr; st=stxr; barrier=''; clobber='' ;;
                acquire) ld=ldaxr; st=stxr; barrier=''; clobber=': "memory"' ;;
                release) ld=ldxr; st=stlxr; barrier=''; clobber=': "memory"' ;;
                full) ld=ldxr; st=stlxr; barrier=' dmb ish\n'; clobber=': "memory"' ;;
            esac
            symbol=$(printf 'cmpxchg_%s_%s' "$bits" "$mode")
            cat >> "$work/aarch64-cmpxchg.i" <<SRC
$typ $symbol(volatile void *p, unsigned long expected, $typ replacement) {
    unsigned long status;
    $typ observed;
    __asm__ __volatile__(" prfm pstl1strm, %[v]\\n"
                         "1: $ld $oldval, %[v]\\n"
                         " eor $tmp, $oldval, $oldparam\\n"
                         " cbnz $tmp, 2f\\n"
                         " $st %w[tmp], $newparam, %[v]\\n"
                         " cbnz %w[tmp], 1b\\n"
                         "$barrier" "2:"
                         : [tmp] "=&r"(status), [oldval] "=&r"(observed),
                           [v] "+Q"(*($typ *)p)
                         : [old] "$constraint"(expected), [new] "r"(replacement)
                         $clobber);
    return observed;
}
SRC
        done
    done
    cat >> "$work/aarch64-cmpxchg.i" <<'SRC'
int main(void) {
    unsigned int a=1; unsigned long b=1;
    unsigned int a0=cmpxchg_32_relaxed(&a,1,2);
    unsigned int a1=cmpxchg_32_acquire(&a,2,4);
    unsigned int a2=cmpxchg_32_release(&a,0,9);
    unsigned int a3=cmpxchg_32_full(&a,4,8);
    unsigned long b0=cmpxchg_64_relaxed(&b,1,2);
    unsigned long b1=cmpxchg_64_acquire(&b,2,4);
    unsigned long b2=cmpxchg_64_release(&b,0,9);
    unsigned long b3=cmpxchg_64_full(&b,4,8);
    return a==8 && b==8 &&
           a0==1 && a1==2 && a2==4 && a3==4 &&
           b0==1 && b1==2 && b2==4 && b3==4 ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/aarch64-cmpxchg.i" \
        -o "$work/aarch64-cmpxchg.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-cmpxchg.i" \
        -o "$work/aarch64-cmpxchg.minic.s"
    for op in 'ldxr w9' 'ldaxr w9' 'ldxr x9' 'ldaxr x9' \
              'stxr w13' 'stlxr w13' 'cbnz w13, 2f' 'dmb ish'; do
        grep -Fq "$op" "$work/aarch64-cmpxchg.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-cmpxchg.minic.s" \
        -o "$work/aarch64-cmpxchg.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu \
            "$work/aarch64-cmpxchg.$flavor.elf"
    done
    for bad in old_constraint register_output changed_alu extra_opcode no_clobber; do
        c=Kr; out='=&r'; alu=eor; injected=''; clobber=': "memory"'
        case "$bad" in
            old_constraint) c=r ;;
            register_output) out='=r' ;;
            changed_alu) alu=orr ;;
            extra_opcode) injected='; svc #0' ;;
            no_clobber) clobber='' ;;
        esac
        cat > "$work/aarch64-cmpxchg-bad-$bad.i" <<SRC
void bad(volatile unsigned int *p, unsigned long expected, unsigned int replacement) {
    unsigned long status; unsigned int observed;
    __asm__ __volatile__(" prfm pstl1strm, %[v]\\n"
        "1: ldxr %w[oldval], %[v]\\n"
        " $alu %w[tmp], %w[oldval], %w[old]\\n"
        " cbnz %w[tmp], 2f\\n"
        " stlxr %w[tmp], %w[new], %[v]\\n"
        " cbnz %w[tmp], 1b\\n"
        " dmb ish\\n2:$injected"
        : [tmp] "$out"(status), [oldval] "=&r"(observed), [v] "+Q"(*p)
        : [old] "$c"(expected), [new] "r"(replacement) $clobber);
}
SRC
        if "$MINIC_CC" --target=aarch64-linux -S \
            "$work/aarch64-cmpxchg-bad-$bad.i" \
            -o "$work/aarch64-cmpxchg-bad-$bad.s" \
            >"$work/aarch64-cmpxchg-bad-$bad.out" \
            2>"$work/aarch64-cmpxchg-bad-$bad.err"; then
            echo "FAIL AArch64 CMPXCHG invalid template admitted: $bad" >&2
            exit 1
        fi
    done
    echo 'MINIC_AARCH64_LINUX_CMPXCHG_GNU_QEMU=PASS eight_modes=8 mismatch=1 negative=5'



fi

# Original Linux syscall wrappers have deliberately different argument C
# types, but LoongArch64 passes each via a 64-bit integer argument register.
# Verify a forward symbol alias and a genuinely executed ABI boundary.
if [ "$target" = loongarch64 ]; then
    cat > "$work/syscall-alias.i" <<'SRC'
extern long sys_entry(unsigned int id, const char *ptr)
    __attribute__((alias("__se_sys_entry")));
long __se_sys_entry(long id, long ptr) {
    return id + (ptr != 0);
}
int main(void) { return sys_entry(41U, (const char *) 1) == 42 ? 0 : 1; }
SRC
    "$cross_cc" -Wno-attribute-alias -x c "$work/syscall-alias.i" \
        -o "$work/syscall.gnu.elf"
    "$MINIC_CC" --target=loongarch64-linux -S "$work/syscall-alias.i" \
        -o "$work/syscall.minic.s"
    grep -Fq '.set sys_entry, __se_sys_entry' "$work/syscall.minic.s"
    "$cross_cc" -x assembler "$work/syscall.minic.s" \
        -o "$work/syscall.minic.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/syscall.gnu.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/syscall.minic.elf"
    cat > "$work/invalid-alias.i" <<'SRC'
extern long float_abi(float x) __attribute__((alias("word_abi")));
long word_abi(long x) { return x; }
SRC
    if "$MINIC_CC" --target=loongarch64-linux -S "$work/invalid-alias.i" \
        -o "$work/invalid-alias.s" >"$work/invalid-alias.out" \
        2>"$work/invalid-alias.err"; then
        echo 'FAIL LoongArch floating-point ABI alias incorrectly accepted' >&2
        exit 1
    fi
    grep -Fq 'GNU function alias requires' "$work/invalid-alias.err"
    # The positive kernel syscall alias above is genuine GNU/QEMU evidence.
    # The dedicated matrix additionally compares GCC source acceptance with
    # MiniC's exact Loong-only symbol-family/ABI admission and fail-closed
    # behavior for unrelated external aliases and unsafe ABI transfers.
    MINIC_CC="$MINIC_CC" HOST_CC="$cross_cc" \
        sh "$root/tests/compiler/c0/probe-loongarch64-syscall-alias-policy.sh"
    echo 'MINIC_LOONGARCH64_FORWARD_SYSCALL_ALIAS_GNU_QEMU=PASS'
    cat > "$work/dbar.i" <<'SRC'
static void release_barrier(void) {
    __asm__ __volatile__("dbar %0 " : : "I"(0x12) : "memory");
}
int main(void) { release_barrier(); return 0; }
SRC
    "$cross_cc" -x c "$work/dbar.i" -o "$work/dbar.gnu.elf"
    "$MINIC_CC" --target=loongarch64-linux -S "$work/dbar.i" -o "$work/dbar.minic.s"
    grep -Fq 'dbar 18' "$work/dbar.minic.s"
    "$cross_cc" -x assembler "$work/dbar.minic.s" -o "$work/dbar.minic.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/dbar.gnu.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/dbar.minic.elf"
    for invalid in suffix constraint nonconstant; do
        case "$invalid" in
          suffix) asm='__asm__ __volatile__("dbar %0; syscall" : : "I"(18) : "memory");' ;;
          constraint) asm='__asm__ __volatile__("dbar %0" : : "r"(18) : "memory");' ;;
          nonconstant) asm='__asm__ __volatile__("dbar %0" : : "I"(v) : "memory");' ;;
        esac
        # Exercise the unsupported assembly in the public live entry.
        # A source-only -S build may intentionally elide unreferenced
        # helper definitions; that must not produce a false negative here.
        printf 'int main(int v) { %s return 0; }\n' \
            "$asm" > "$work/dbar-$invalid.i"
        if "$MINIC_CC" --target=loongarch64-linux -S "$work/dbar-$invalid.i" \
            -o "$work/dbar-$invalid.s" >"$work/dbar-$invalid.out" \
            2>"$work/dbar-$invalid.err"; then
            echo "FAIL LoongArch unsafe dbar template admission $invalid" >&2
            sed -n '1,85p' "$work/dbar-$invalid.s" >&2 || true
            cat "$work/dbar-$invalid.err" >&2 || true
            exit 1
        fi
    done
    cat > "$work/dbar-max.i" <<'SRC'
static void barrier(void) {
    __asm__ __volatile__("dbar %0" : : "I"(0x700) : "memory");
}
int main(void) { barrier(); return 0; }
SRC
    "$MINIC_CC" --target=loongarch64-linux -S "$work/dbar-max.i" \
        -o "$work/dbar-max.s"
    grep -Fq 'dbar 1792' "$work/dbar-max.s"
    "$cross_cc" -x assembler "$work/dbar-max.s" -o "$work/dbar-max.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/dbar-max.elf"
    echo 'MINIC_LOONGARCH64_DBAR_GNU_QEMU=PASS tested=2 rejected=3'

    # The ORIGINAL LoongArch Linux 6.6.143 per-CPU offset register is
    # file-scope asm("$r21"). A zero-byte volatile +r MUST NOT replace the
    # later hardware register read with zero, memory or an ordinary C local.
    # This is common GNU C barrier semantics, while ISA registers are
    # still exclusively owned by the LoongArch target.
    cat > "$work/loong-fixed-empty-percpu.i" <<'SRC'
register unsigned long __my_cpu_offset __asm__("$r21");
extern int numa_node;
static inline unsigned long cpu_offset(void) {
    __asm__ __volatile__("" : "+r"(__my_cpu_offset));
    return __my_cpu_offset;
}
int numa_node_id(void) {
    unsigned long addr;
    __asm__ ("" : "=r"(addr) : "0"((unsigned long)&numa_node));
    return *(int *)(addr + cpu_offset());
}
SRC
    "$cross_cc" -c -x c "$work/loong-fixed-empty-percpu.i" \
        -o "$work/loong-fixed-empty-percpu.gnu.o"
    "$MINIC_CC" --target=loongarch64-linux -S \
        "$work/loong-fixed-empty-percpu.i" \
        -o "$work/loong-fixed-empty-percpu.minic.s"
    grep -Fq '$r21' "$work/loong-fixed-empty-percpu.minic.s"
    "$cross_cc" -c -x assembler "$work/loong-fixed-empty-percpu.minic.s" \
        -o "$work/loong-fixed-empty-percpu.minic.o"
    loongarch64-linux-gnu-objdump -dr \
        "$work/loong-fixed-empty-percpu.minic.o" > \
        "$work/loong-fixed-empty-percpu.dis"
    grep -Fq 'r21' "$work/loong-fixed-empty-percpu.dis"
    cat > "$work/loong-invalid-empty-percpu.i" <<'SRC'
register unsigned long __my_cpu_offset __asm__("$r21");
unsigned long unsafe_cpu_offset(void) {
    __asm__ __volatile__("syscall 0" : "+r"(__my_cpu_offset));
    return __my_cpu_offset;
}
SRC
    if "$MINIC_CC" --target=loongarch64-linux -S \
        "$work/loong-invalid-empty-percpu.i" \
        -o "$work/loong-invalid-empty-percpu.s" >/dev/null 2>&1; then
        echo 'FAIL LoongArch unqualified nonempty r21 GNU asm admitted' >&2
        exit 1
    fi
    echo 'MINIC_LOONGARCH64_LINUX_PERCPU_R21_EMPTY_GNU_AS=PASS source_exact=1 hardware_read=1 negative=1'

    # Original GNU Linux pgtable.h PTE buddy bit LL/SC; true GNU and
    # MiniC cross GAS+QEMU with 0→GLOBAL and preexisting-nonzero branches.
    cat > "$work/loong-pte.i" <<'SRC'
typedef struct { unsigned long pte; } pte_t;
static void set_pte(pte_t *ptep,pte_t value) {
    *ptep=value;
    if(value.pte&(1UL<<6)) {
        pte_t *buddy=(pte_t *)((unsigned long)ptep ^ sizeof(pte_t));
        unsigned long global=1UL<<6,tmp;
        __asm__ __volatile__(
        "1:" "ll.d\t" "%[tmp], %[buddy]\t\t\n"
        "\tbnez\t%[tmp], 2f\t\t\t\n"
        "\t or\t%[tmp], %[tmp], %[global]\t\n"
        "sc.d\t" "%[tmp], %[buddy]\t\t\n"
        "\tbeqz\t%[tmp], 1b\t\t\t\n"
        "\tnop\t\t\t\t\t\n"
        "2:\t\t\t\t\t\t\n"
        "\tdbar 0x700\t\n"
        : [buddy] "+m"(buddy->pte),[tmp] "=&r"(tmp)
        : [global] "r"(global));
    }
}
int main(void) {
    pte_t pair[2] __attribute__((aligned(16)))={{0UL},{0UL}};
    set_pte(&pair[0],(pte_t){64UL});
    if(pair[0].pte!=64UL||pair[1].pte!=64UL)return 1;
    pair[1].pte=3UL;
    set_pte(&pair[0],(pte_t){64UL});
    if(pair[1].pte!=3UL)return 2;
    return 0;
}
SRC
    "$cross_cc" -O0 -x c "$work/loong-pte.i" \
        -o "$work/loong-pte.gnu.elf"
    "$MINIC_CC" --target=loongarch64-linux -S \
        "$work/loong-pte.i" -o "$work/loong-pte.minic.s"
    for op in 'll.d $t4' 'sc.d $t4' 'beqz $t4' 'dbar 0x700'; do
        grep -Fq "$op" "$work/loong-pte.minic.s"
    done
    "$cross_cc" -x assembler "$work/loong-pte.minic.s" \
        -o "$work/loong-pte.minic.elf"
    for flavor in gnu minic; do
        qemu-loongarch64 -L /usr/loongarch64-linux-gnu \
            "$work/loong-pte.$flavor.elf"
    done
    sed 's/dbar 0x700/dbar 0x700; syscall 0/' \
        "$work/loong-pte.i" > "$work/loong-pte-bad.i"
    if "$MINIC_CC" --target=loongarch64-linux -S \
        "$work/loong-pte-bad.i" -o "$work/loong-pte-bad.s" \
        >/dev/null 2>&1; then
        echo 'FAIL Loong PTE LL/SC injected syscall admitted';exit 1
    fi
    echo 'MINIC_LOONGARCH64_LINUX_PTE_LLSC_GNU_QEMU=PASS paths=2 negative=1'

    # Original arch/loongarch/include/asm/irqflags.h uses privileged CRMD CSR.
    # GNU and MiniC must generate *real* csrrd/csrxchg machine instructions.
    # Privileged instructions cannot run in QEMU-user: object/GAS comparison
    # is the oracle here; every negative template must fail at -S.
    cat > "$work/loong-irq-csr.i" <<'SRC'
typedef unsigned int u32;
u32 read_flags(void) {
    u32 flags;
    __asm__ __volatile__("csrrd %[val], %[reg]\n\t"
                         : [val] "=r"(flags)
                         : [reg] "i"(0) : "memory");
    return flags;
}
u32 save_flags(void) {
    u32 flags = 0;
    register u32 mask asm("t0") = 4;
    __asm__ __volatile__("csrxchg %[val], %[mask], %[reg]\n\t"
                         : [val] "+r"(flags)
                         : [mask] "r"(mask), [reg] "i"(0) : "memory");
    return flags;
}
void restore_flags(unsigned long f) {
    register u32 mask asm("t0") = 4;
    __asm__ __volatile__("csrxchg %[val], %[mask], %[reg]\n\t"
                         : [val] "+r"(f)
                         : [mask] "r"(mask), [reg] "i"(0) : "memory");
}
int main(void) { return 0; }
SRC
    "$cross_cc" -c -x c "$work/loong-irq-csr.i" -o "$work/loong-irq-csr.gnu.o"
    "$MINIC_CC" --target=loongarch64-linux -S "$work/loong-irq-csr.i" \
        -o "$work/loong-irq-csr.minic.s"
    grep -Fq 'csrrd $t1, 0' "$work/loong-irq-csr.minic.s"
    grep -Fq 'csrxchg $t1, $t0, 0' "$work/loong-irq-csr.minic.s"
    "$cross_cc" -c -x assembler "$work/loong-irq-csr.minic.s" \
        -o "$work/loong-irq-csr.minic.o"
    for flavor in gnu minic; do
        loongarch64-linux-gnu-objdump -d "$work/loong-irq-csr.$flavor.o" |
            grep -q 'csrxchg'
        loongarch64-linux-gnu-objdump -d "$work/loong-irq-csr.$flavor.o" |
            grep -q 'csrrd'
    done
    for invalid in csr-index register-constraint opcode-suffix nonconstant; do
        case "$invalid" in
            csr-index) asm='__asm__ __volatile__("csrrd %[val], %[reg]" : [val] "=r"(flags) : [reg] "i"(1) : "memory");';;
            register-constraint) asm='__asm__ __volatile__("csrrd %[val], %[reg]" : [val] "=m"(flags) : [reg] "i"(0) : "memory");';;
            opcode-suffix) asm='__asm__ __volatile__("csrrd %[val], %[reg]; syscall 0" : [val] "=r"(flags) : [reg] "i"(0) : "memory");';;
            nonconstant) asm='__asm__ __volatile__("csrrd %[val], %[reg]" : [val] "=r"(flags) : [reg] "i"(n) : "memory");';;
        esac
        printf 'int main(int n) { unsigned int flags=0; %s return (int)flags; }\n' \
            "$asm" > "$work/loong-csr-bad-$invalid.i"
        if "$MINIC_CC" --target=loongarch64-linux -S \
            "$work/loong-csr-bad-$invalid.i" \
            -o "$work/loong-csr-bad-$invalid.s" \
            >"$work/loong-csr-bad-$invalid.out" \
            2>"$work/loong-csr-bad-$invalid.err"; then
            echo "FAIL unsafe Loong CRMD CSR accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_LOONGARCH64_LINUX_CSR_CRMD_GNU_AS=PASS operations=2 negative=4 privileged_only=1'

    # Exact original upstream Linux atomic.h +ZB/r store-only hardware AMO
    # family. One Common Core structured memory-RMW+scalar-input pipeline,
    # target-owned LP64D machine emission and independent GNU/QEMU oracle.
    cat > "$work/atomic-kernel.i" <<'SRC'
struct atomic32 { int counter; };
struct atomic64 { long long counter; };
static void aand32(int n, struct atomic32 *p) {
    __asm__ __volatile__("amand_db.w $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aor32(int n, struct atomic32 *p) {
    __asm__ __volatile__("amor_db.w $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void axor32(int n, struct atomic32 *p) {
    __asm__ __volatile__("amxor_db.w $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aadd32(int n, struct atomic32 *p) {
    __asm__ __volatile__("amadd_db.w $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aand64(long long n, struct atomic64 *p) {
    __asm__ __volatile__("amand_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aor64(long long n, struct atomic64 *p) {
    __asm__ __volatile__("amor_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void axor64(long long n, struct atomic64 *p) {
    __asm__ __volatile__("amxor_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aadd64(long long n, struct atomic64 *p) {
    __asm__ __volatile__("amadd_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
int main(void) {
    struct atomic32 a = {0x31};
    struct atomic64 b = {0x31};
    aand32(0x30, &a);
    aor32(0x06, &a);
    axor32(0x05, &a);
    aadd32(10, &a);
    aand64(0x30, &b);
    aor64(0x06, &b);
    axor64(0x05, &b);
    aadd64(10, &b);
    return a.counter == 0x3d && b.counter == 0x3d ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/atomic-kernel.i" -o "$work/atomic-kernel.gnu.elf"
    "$MINIC_CC" --target=loongarch64-linux -S "$work/atomic-kernel.i" \
        -o "$work/atomic-kernel.minic.s"
    for mnemonic in amand_db.w amor_db.w amxor_db.w amadd_db.w \
                    amand_db.d amor_db.d amxor_db.d amadd_db.d; do
        grep -Fq "$mnemonic \$zero, \$t0, \$t1" "$work/atomic-kernel.minic.s"
    done
    "$cross_cc" -x assembler "$work/atomic-kernel.minic.s" \
        -o "$work/atomic-kernel.minic.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/atomic-kernel.gnu.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/atomic-kernel.minic.elf"
    for invalid in suffix constraint reg_arg no_memory wrong_width; do
        case "$invalid" in
          suffix) asm='__asm__ __volatile__("amand_db.d $zero, %1, %0; syscall" : "+ZB"(v) : "r"(n) : "memory");' ;;
          constraint) asm='__asm__ __volatile__("amand_db.d $zero, %1, %0" : "+m"(v) : "r"(n) : "memory");' ;;
          reg_arg) asm='__asm__ __volatile__("amand_db.d $zero, %1, %0" : "+ZB"(v) : "I"(1) : "memory");' ;;
          no_memory) asm='__asm__ __volatile__("amand_db.d $zero, %1, %0" : "+ZB"(v) : "r"(n));' ;;
          wrong_width) asm='__asm__ __volatile__("amand_db.d $zero, %1, %0" : "+ZB"(v32) : "r"(n) : "memory");' ;;
        esac
        printf 'int main(void) { long v = 7, n = 1; int v32 = 7; %s return 0; }\n' \
            "$asm" > "$work/atomic-invalid-$invalid.i"
        if "$MINIC_CC" --target=loongarch64-linux -S \
            "$work/atomic-invalid-$invalid.i" \
            -o "$work/atomic-invalid-$invalid.s" \
            >"$work/atomic-invalid-$invalid.out" \
            2>"$work/atomic-invalid-$invalid.err"; then
            echo "FAIL LoongArch unsafe hardware atomic admitted: $invalid" >&2
            sed -n '1,100p' "$work/atomic-invalid-$invalid.s" >&2 || true
            exit 1
        fi
        test ! -e "$work/atomic-invalid-$invalid.s"
    done
    echo 'MINIC_LOONGARCH64_ATOMIC_GNU_QEMU=PASS variants=8 negative=5 hardware=1'

    # Original Linux 6.6 atomic{,64}_fetch_{and,or,xor,add}_relaxed:
    # the AMO result is the OLD in-memory value; software load/store is invalid.
    cat > "$work/atomic-fetch-kernel.i" <<'SRC'
struct atomic32 { int counter; };
struct atomic64 { long long counter; };
static int fetch_and32(int n,struct atomic32 *p){
  int old; __asm__ __volatile__("amand_db.w %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static int fetch_or32(int n,struct atomic32 *p){
  int old; __asm__ __volatile__("amor_db.w %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static int fetch_xor32(int n,struct atomic32 *p){
  int old; __asm__ __volatile__("amxor_db.w %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static int fetch_add32(int n,struct atomic32 *p){
  int old; __asm__ __volatile__("amadd_db.w %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static long long fetch_and64(long long n,struct atomic64 *p){
  long long old; __asm__ __volatile__("amand_db.d %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static long long fetch_or64(long long n,struct atomic64 *p){
  long long old; __asm__ __volatile__("amor_db.d %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static long long fetch_xor64(long long n,struct atomic64 *p){
  long long old; __asm__ __volatile__("amxor_db.d %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
static long long fetch_add64(long long n,struct atomic64 *p){
  long long old; __asm__ __volatile__("amadd_db.d %1, %2, %0\n"
          : "+ZB"(p->counter), "=&r"(old) : "r"(n) : "memory"); return old;
}
int main(void){
  struct atomic32 a={0x31}; struct atomic64 b={0x31};
  int x=fetch_and32(0x30,&a),y=fetch_or32(6,&a);
  int z=fetch_xor32(5,&a),w=fetch_add32(10,&a);
  long long q=fetch_and64(0x30,&b),r=fetch_or64(6,&b);
  long long t=fetch_xor64(5,&b),u=fetch_add64(10,&b);
  return x==0x31&&y==0x30&&z==0x36&&w==0x33&&a.counter==0x3d
      &&q==0x31&&r==0x30&&t==0x36&&u==0x33&&b.counter==0x3d ? 0 : 1;
}
SRC
    "$cross_cc" -O0 -x c "$work/atomic-fetch-kernel.i" -o "$work/atomic-fetch-kernel.gnu.elf"
    "$MINIC_CC" --target=loongarch64-linux -S "$work/atomic-fetch-kernel.i" \
        -o "$work/atomic-fetch-kernel.minic.s"
    for mnemonic in amand_db.w amor_db.w amxor_db.w amadd_db.w \
                    amand_db.d amor_db.d amxor_db.d amadd_db.d; do
        grep -Fq "$mnemonic \$t2, \$t0, \$t1" "$work/atomic-fetch-kernel.minic.s"
    done
    "$cross_cc" -x assembler "$work/atomic-fetch-kernel.minic.s" \
        -o "$work/atomic-fetch-kernel.minic.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/atomic-fetch-kernel.gnu.elf"
    qemu-loongarch64 -L /usr/loongarch64-linux-gnu "$work/atomic-fetch-kernel.minic.elf"
    for invalid in suffix constraint oldval_input no_memory wrong_width; do
        case "$invalid" in
          suffix) asm='__asm__ __volatile__("amand_db.d %1, %2, %0; syscall" : "+ZB"(v), "=&r"(old) : "r"(n) : "memory");' ;;
          constraint) asm='__asm__ __volatile__("amand_db.d %1, %2, %0" : "+m"(v), "=&r"(old) : "r"(n) : "memory");' ;;
          oldval_input) asm='__asm__ __volatile__("amand_db.d %1, %2, %0" : "+ZB"(v), "=m"(old) : "r"(n) : "memory");' ;;
          no_memory) asm='__asm__ __volatile__("amand_db.d %1, %2, %0" : "+ZB"(v), "=&r"(old) : "r"(n));' ;;
          wrong_width) asm='__asm__ __volatile__("amand_db.d %1, %2, %0" : "+ZB"(v32), "=&r"(old) : "r"(n) : "memory");' ;;
        esac
        printf 'int main(void){ long v=7,n=1,old=0; int v32=7; %s return 0; }\n' \
            "$asm" > "$work/atomic-fetch-invalid-$invalid.i"
        if "$MINIC_CC" --target=loongarch64-linux -S \
            "$work/atomic-fetch-invalid-$invalid.i" \
            -o "$work/atomic-fetch-invalid-$invalid.s" \
            >"$work/atomic-fetch-invalid-$invalid.out" \
            2>"$work/atomic-fetch-invalid-$invalid.err"; then
            echo "FAIL unqualified fetch-AMO accepted: $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_LOONGARCH64_LINUX_FETCH_AMO_GNU_QEMU=PASS variants=8 negative=5 old_value=1'
fi

# Architecture-specific machine syntax is still owned by real GNU AS.
echo "MINIC_FILE_ASM_GNU_AS=PASS target=$target original_gnu=1 original_bytes=1 sections=1 function_aliases=2 object_alias=1"
