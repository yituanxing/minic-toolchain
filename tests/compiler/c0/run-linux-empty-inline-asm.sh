#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
work=${BUILD_DIR:-"$root/build/debug"}/tests/minic-linux-empty-asm
mkdir -p "$work"
cat > "$work/empty.i" <<'SRC'
typedef unsigned long size_t;
extern void *memset(void *, int, size_t);
static inline __attribute__((__gnu_inline__, __unused__, __no_instrument_function__))
void memzero_explicit(void *s, size_t count) {
  memset(s, 0, count);
  __asm__ __volatile__("" : : "r"(s) : "memory");
}
struct bpf_fentry_test_t { struct bpf_fentry_test_t *a; };
int __attribute__((__noinline__))
bpf_fentry_test7(struct bpf_fentry_test_t *arg) {
  asm volatile("" : "+r"(arg));
  return (long)arg;
}
int main(void) {
  char buf[8] = {42};
  struct bpf_fentry_test_t *p = (struct bpf_fentry_test_t *)42;
  memzero_explicit(buf, sizeof(buf));
  return (int)buf[0] + (bpf_fentry_test7(p) == 42 ? 42 : 1);
}
SRC
# Localized L1 differentiators: do not attribute a whole-function Core
# lowering failure to the inline asm until a no-asm, no-cast control passes.
cat > "$work/no-asm-cast.i" <<'SRC'
struct test_t { struct test_t *next; };
int f(struct test_t *arg) { return (long)arg; }
SRC
cat > "$work/asm-return-constant.i" <<'SRC'
struct test_t { struct test_t *next; };
int f(struct test_t *arg) { asm volatile("" : "+r"(arg)); return 42; }
SRC
cat > "$work/asm-return-pointer.i" <<'SRC'
struct test_t { struct test_t *next; };
int f(struct test_t *arg) { asm volatile("" : "+r"(arg)); return (long)arg; }
SRC
cat > "$work/asm-nonvolatile.i" <<'SRC'
struct test_t { struct test_t *next; };
int f(struct test_t *arg) { asm("" : "+r"(arg)); return 42; }
SRC
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
  for name in no-asm-cast asm-return-constant asm-return-pointer asm-nonvolatile; do
    if MINIC_ASM_BATCH_TRACE=1 "$MINIC_CC" --target="$target" -S           "$work/$name.i" -o "$work/$target-$name.s"           >"$work/$target-$name.out" 2>"$work/$target-$name.err"; then
      echo "MINIC_LINUX_EMPTY_ASM_L1 target=$target probe=$name status=PASS"
    else
      echo "MINIC_LINUX_EMPTY_ASM_L1 target=$target probe=$name status=FAIL"
      cat "$work/$target-$name.err"
    fi
  done
done
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
  "$MINIC_CC" --target="$target" -S "$work/empty.i" -o "$work/$target.s"
  test -s "$work/$target.s"
done
cc -std=gnu11 -O0 -x assembler "$work/x86_64-linux.s" -no-pie -o "$work/minic"
cc -std=gnu11 -O0 -x c "$work/empty.i" -no-pie -o "$work/gnu"
set +e
"$work/minic"; a=$?
"$work/gnu"; b=$?
set -e
test "$a" -eq 42
test "$b" -eq 42
printf '%s\n' 'MINIC_LINUX_EMPTY_ASM=PASS five_targets=1 memzero=1 bpf_empty_rw=1 native_diff=42'
