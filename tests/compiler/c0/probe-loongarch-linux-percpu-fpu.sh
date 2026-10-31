#!/usr/bin/env bash
# Genuine LoongArch Linux 6.6.143 per-CPU __percpu_read/write/add/or/and
# plus kernel_fpu_begin FCSR0 write. All ISA semantics target-owned.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
dir=$(mktemp -d);trap 'rm -rf "$dir"' EXIT
cat > "$dir/percpu.i" <<'SRC'
unsigned long native_read1(void *p) {
 unsigned long ret;
 __asm__ __volatile__("ldx.b %[ret], $r21, %[ptr]\n"
  : [ret] "=&r"(ret) : [ptr] "r"(p) : "memory");
 return ret;
}
unsigned long native_read2(void *p) {
 unsigned long ret; __asm__ __volatile__("ldx.h %[ret], $r21, %[ptr]"
  : [ret] "=&r"(ret) : [ptr] "r"(p) : "memory"); return ret;
}
unsigned long native_read4(void *p) {
 unsigned long ret; __asm__ __volatile__("ldx.w %[ret], $r21, %[ptr]"
  : [ret] "=&r"(ret) : [ptr] "r"(p) : "memory"); return ret;
}
unsigned long native_read8(void *p) {
 unsigned long ret; __asm__ __volatile__("ldx.d %[ret], $r21, %[ptr]"
  : [ret] "=&r"(ret) : [ptr] "r"(p) : "memory"); return ret;
}
void native_write1(void *p, unsigned long x) {
 __asm__ __volatile__("stx.b %[val], $r21, %[ptr]" : :
  [val] "r"(x), [ptr] "r"(p) : "memory");
}
void native_write2(void *p, unsigned long x) {
 __asm__ __volatile__("stx.h %[val], $r21, %[ptr]" : :
  [val] "r"(x), [ptr] "r"(p) : "memory");
}
void native_write4(void *p, unsigned long x) {
 __asm__ __volatile__("stx.w %[val], $r21, %[ptr]" : :
  [val] "r"(x), [ptr] "r"(p) : "memory");
}
void native_write8(void *p, unsigned long x) {
 __asm__ __volatile__("stx.d %[val], $r21, %[ptr]" : :
  [val] "r"(x), [ptr] "r"(p) : "memory");
}
unsigned long native_amo_add4(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amadd.w %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned int *)p)
  : [val] "r"(x)); return ret+x;
}
unsigned long native_amo_add8(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amadd.d %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned long long *)p)
  : [val] "r"(x)); return ret+x;
}
unsigned long native_amo_and4(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amand.w %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned int *)p)
  : [val] "r"(x)); return ret;
}
unsigned long native_amo_and8(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amand.d %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned long long *)p)
  : [val] "r"(x)); return ret;
}
unsigned long native_amo_or4(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amor.w %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned int *)p)
  : [val] "r"(x)); return ret;
}
unsigned long native_amo_or8(void *p, unsigned long x) {
 unsigned long ret;
 __asm__ __volatile__("amor.d %[ret], %[val], %[ptr]"
  : [ret] "=&r"(ret), [ptr] "+ZB"(*(unsigned long long *)p)
  : [val] "r"(x)); return ret;
}
void native_kernel_fpu_reset(void) {
 __asm__ __volatile__("movgr2fcsr $fcsr0, %0" : : "r"(0));
}
SRC
if ! MINIC_CORE_FAILURE_CENSUS=1 MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 \
    "$mini" --target=loongarch64-linux -S "$dir/percpu.i" -o "$dir/mini.s" > "$dir/output" 2>&1; then
    echo "LOONG_PERCPU_FULL_FIXTURE_BACKEND_FRONTIER=FAIL" >&2
    grep -E 'MINIC_BACKEND_REJECT|MINIC_M8_OPCODE_REJECT|MINIC_CORE_FUNCTION_FAILURE|MINIC_BACKEND_ASM_FORM|error:' "$dir/output" | tail -35 >&2 || :
    exit 1
fi
for op in ldx.b ldx.h ldx.w ldx.d stx.b stx.h stx.w stx.d \
          amadd.w amadd.d amand.w amand.d amor.w amor.d movgr2fcsr; do
  grep -Fq "$op " "$dir/mini.s"
done
if [[ "${MINIC_LOONG_PERCPU_SOURCE_ONLY:-0}" != 1 ]]; then
  loongarch64-linux-gnu-gcc-13 -c -x assembler "$dir/mini.s" -o "$dir/mini.o"
  loongarch64-linux-gnu-gcc-13 -c -x c "$dir/percpu.i" -o "$dir/gnu.o"
  loongarch64-linux-gnu-objdump -d "$dir/mini.o" > "$dir/mini.dis"
  loongarch64-linux-gnu-objdump -d "$dir/gnu.o" > "$dir/gnu.dis"
  for op in ldx.b ldx.h ldx.w ldx.d stx.b stx.h stx.w stx.d \
            amadd.w amadd.d amand.w amand.d amor.w amor.d movgr2fcsr; do
    grep -Fq "$op" "$dir/mini.dis"
    grep -Fq "$op" "$dir/gnu.dis"
  done
fi
for bad in injected wrong-base wrong-clobber wrong-output;do
  case "$bad" in
    injected) sed 's/amadd.w %\[ret\]/amadd.w %[ret]; nop/' "$dir/percpu.i" > "$dir/bad.i" ;;
    wrong-base) sed 's/\$r21, %\[ptr\]/$r20, %[ptr]/' "$dir/percpu.i" > "$dir/bad.i" ;;
    wrong-clobber) sed 's/movgr2fcsr \$fcsr0, %0/movgr2fcsr $fcsr0, %0; nop/' "$dir/percpu.i" > "$dir/bad.i" ;;
    wrong-output) sed 's/\[ret\] "=&r"(ret)/[ret] "=m"(ret)/' "$dir/percpu.i" > "$dir/bad.i" ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$dir/bad.i" -o "$dir/bad.s" >/dev/null 2>&1;then
    echo "UNQUALIFIED_ORIGINAL_LINUX_PERCPU=$bad" >&2
    exit 1
  fi
  test ! -e "$dir/bad.s"
done
echo "MINIC_LOONGARCH_PERCPU=PASS original_linux_forms=15 ISA_indexed=8 ISA_AMO=6 FCSR0=1 negatives=4 GAS_mode=${MINIC_LOONG_PERCPU_SOURCE_ONLY:-0}"
