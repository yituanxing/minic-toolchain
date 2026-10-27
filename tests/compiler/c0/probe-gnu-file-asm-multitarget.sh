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

    # Exact original arm64 6.6 atomic_ll_sc.h AND/OR/XOR, 32 and 64-bit.
    # LLVM/AS-style raw passthrough is forbidden: target must consume the
    # canonical existing 4 Core register/memory/scalar roles.
    : > "$work/aarch64-llsc.i"
    for width in 32 64; do
        if [ "$width" = 32 ]; then
            typename='unsigned int'; prefix=''; word0='%w0'; word3='%w3'
        else
            typename='unsigned long'; prefix='64'; word0='%0'; word3='%3'
        fi
        for operation in and or xor; do
            case "$operation" in and) opcode=and;; or) opcode=orr;; xor) opcode=eor;; esac
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
                       : "=&r"(result), "=&r"(tmp), "+Q"(*p) : "Lr"(i));
}
SRC
        done
    done
    cat >> "$work/aarch64-llsc.i" <<'SRC'
int main(void) {
    unsigned int a = 0x31;
    unsigned long b = 0x31;
    atomic_32_or(&a, 6); atomic_32_and(&a, 0x30); atomic_32_xor(&a, 5);
    atomic_64_or(&b, 6); atomic_64_and(&b, 0x30); atomic_64_xor(&b, 5);
    return a == 0x35 && b == 0x35 ? 0 : 1;
}
SRC
    "$cross_cc" -x c "$work/aarch64-llsc.i" -o "$work/aarch64-llsc.gnu.elf"
    "$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-llsc.i" \
        -o "$work/aarch64-llsc.minic.s"
    for op in and orr eor; do
        grep -Fq "  $op w9, w9, w11" "$work/aarch64-llsc.minic.s"
        grep -Fq "  $op x9, x9, x11" "$work/aarch64-llsc.minic.s"
    done
    "$cross_cc" -x assembler "$work/aarch64-llsc.minic.s" \
        -o "$work/aarch64-llsc.minic.elf"
    for flavor in gnu minic; do
        qemu-aarch64 -L /usr/aarch64-linux-gnu "$work/aarch64-llsc.$flavor.elf"
    done
    for invalid in wrong_constraint hidden_svc wrong_label wrong_output; do
        case "$invalid" in
            wrong_constraint) ic='r'; suffix=''; label='1b'; out='=&r' ;;
            hidden_svc) ic='Lr'; suffix='; svc #0'; label='1b'; out='=&r' ;;
            wrong_label) ic='Lr'; suffix=''; label='2b'; out='=&r' ;;
            wrong_output) ic='Lr'; suffix=''; label='1b'; out='=r' ;;
        esac
        cat > "$work/aarch64-llsc-invalid-$invalid.i" <<SRC
void bad(unsigned long *p, unsigned long i) {
  unsigned long result, tmp;
  __asm__ __volatile__("// atomic64_or\\n"
    " prfm pstl1strm, %2\\n"
    "1: ldxr %0, %2\\n"
    " orr %0, %0, %3\\n"
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
    echo 'MINIC_AARCH64_LINUX_LLSC_GNU_AS_QEMU=PASS operations=6 negatives=4'
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

    # Exact original upstream Linux atomic.h +ZB/r store-only hardware AMO
    # family. One Common Core structured memory-RMW+scalar-input pipeline,
    # target-owned LP64D machine emission and independent GNU/QEMU oracle.
    cat > "$work/atomic-kernel.i" <<'SRC'
struct atomic32 { int counter; };
struct atomic64 { long counter; };
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
static void aand64(long n, struct atomic64 *p) {
    __asm__ __volatile__("amand_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aor64(long n, struct atomic64 *p) {
    __asm__ __volatile__("amor_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void axor64(long n, struct atomic64 *p) {
    __asm__ __volatile__("amxor_db.d $zero, %1, %0\n"
                         : "+ZB"(p->counter) : "r"(n) : "memory");
}
static void aadd64(long n, struct atomic64 *p) {
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
fi

# Architecture-specific machine syntax is still owned by real GNU AS.
echo "MINIC_FILE_ASM_GNU_AS=PASS target=$target original_gnu=1 original_bytes=1 sections=1 function_aliases=2 object_alias=1"
