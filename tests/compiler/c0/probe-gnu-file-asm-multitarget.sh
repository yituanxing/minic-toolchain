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
        printf 'void invalid(int v) { %s }\n' "$asm" > "$work/dbar-$invalid.i"
        if "$MINIC_CC" --target=loongarch64-linux -S "$work/dbar-$invalid.i" \
            -o "$work/dbar-$invalid.s" >"$work/dbar-$invalid.out" \
            2>"$work/dbar-$invalid.err"; then
            echo "FAIL LoongArch unsafe dbar template admission $invalid" >&2
            exit 1
        fi
    done
    echo 'MINIC_LOONGARCH64_DBAR_GNU_QEMU=PASS tested=1 rejected=3'
fi

# Architecture-specific machine syntax is still owned by real GNU AS.
echo "MINIC_FILE_ASM_GNU_AS=PASS target=$target original_gnu=1 original_bytes=1 sections=1 function_aliases=2 object_alias=1"
