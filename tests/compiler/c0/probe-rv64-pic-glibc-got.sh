#!/bin/sh
# Independent PIC vs static-linux RISC-V addressing oracle.
# Static kernel mode MUST retain PCREL relocations and avoid GOT; GNU/glibc
# dynamic PIE must use GOT for extern data and function addresses.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/source.c" <<'SRC'
extern int optind;
extern int puts(const char *);
extern int printf(const char *, ...);
int *rv_pic_optind(void) { return &optind; }
int (*rv_pic_puts(void))(const char *) { return &puts; }
int main(void) {
    if (rv_pic_optind() != &optind || rv_pic_puts() != &puts)
        return 1;
    printf("RV64_GLIBC_PIE_GOT_OK:%d\\n", optind > 0);
    return 42;
}
SRC
cat > "$work/nonpreemptible.c" <<'SRC'
static int local_data;
static int local_function(void) { return local_data; }
extern int hidden_data __attribute__((visibility("hidden")));
__attribute__((visibility("hidden"))) extern int hidden_function(void);
extern int ordinary_extern;
extern int ordinary_function(void);
int *local_data_address(void) { return &local_data; }
int (*local_function_address(void))(void) { return &local_function; }
int *hidden_data_address(void) { return &hidden_data; }
int (*hidden_function_address(void))(void) { return &hidden_function; }
int *ordinary_data_address(void) { return &ordinary_extern; }
int (*ordinary_function_address(void))(void) { return &ordinary_function; }
const char *boot_chosen_string(void) { return "/chosen"; }
SRC
# -E -P is deliberately used even in apt-free mode; the TU has no headers.
cp "$work/source.c" "$work/source.i"
"$MINIC_CC" --target=riscv64-linux -fPIE -S "$work/source.i" -o "$work/pic.s"
"$MINIC_CC" --target=riscv64-linux -fno-pie -S "$work/source.i" -o "$work/kernel.s"
for pic_flag in -fPIE -fPIC; do
  "$MINIC_CC" --target=riscv64-linux "$pic_flag" -S \
    "$work/nonpreemptible.c" -o "$work/nonpreemptible-$pic_flag.s"
  for symbol in local_data local_function hidden_data hidden_function; do
    grep -Eq "^[[:space:]]+lla t0, $symbol$" "$work/nonpreemptible-$pic_flag.s" || {
      echo "RV64_PIC_GLIBC_GOT=FAIL $pic_flag should use PCREL for $symbol" >&2
      exit 1
    }
  done
  grep -Eq '^[[:space:]]+lla t0, \.Lminic_string_[0-9]+' \
    "$work/nonpreemptible-$pic_flag.s" || {
    echo "RV64_PIC_GLIBC_GOT=FAIL $pic_flag private_string_got_regression=1" >&2
    exit 1
  }
  for symbol in ordinary_extern ordinary_function; do
    grep -Eq "^[[:space:]]+la t0, $symbol$" "$work/nonpreemptible-$pic_flag.s" || {
      echo "RV64_PIC_GLIBC_GOT=FAIL $pic_flag should use GOT for $symbol" >&2
      exit 1
    }
  done
done
"$MINIC_CC" --target=riscv64-linux -fno-pie -S \
  "$work/nonpreemptible.c" -o "$work/nonpreemptible-static.s"
if grep -q '  .option pic' "$work/nonpreemptible-static.s"; then
  echo 'RV64_PIC_GLIBC_GOT=FAIL kernel_pic_got_regression=1' >&2
  exit 1
fi
grep -q '  .option pic' "$work/pic.s"
grep -q '  la t0, optind' "$work/pic.s"
grep -q '  la t0, puts' "$work/pic.s"
grep -q '  lla t0, optind' "$work/kernel.s"
grep -q '  lla t0, puts' "$work/kernel.s"
if grep -q '  .option pic' "$work/kernel.s"; then
  echo 'RV64_PIC_GLIBC_GOT=FAIL static_kernel_got_regression=1' >&2
  exit 1
fi
if [ "${MINIC_PIC_ASM_ONLY:-0}" = 1 ]; then
  echo 'RV64_PIC_GLIBC_GOT=PASS stage=apt_free_core_target preemptible_GOT=1 private_hidden_PCREL=1 kernel_lla=1'
  exit 0
fi
gcc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
sysroot=${RISCV_SYSROOT:-/usr/riscv64-linux-gnu}
"$gcc" -fPIE -pie "$work/source.c" -o "$work/gnu"
"$gcc" -c -x assembler "$work/pic.s" -o "$work/mini.o"
"${gcc%-gcc}-readelf" -Wr "$work/mini.o" >"$work/mini.reloc"
grep -q 'R_RISCV_GOT_HI20.*optind' "$work/mini.reloc"
grep -q 'R_RISCV_GOT_HI20.*puts' "$work/mini.reloc"
"$gcc" -c -x assembler "$work/nonpreemptible--fPIE.s" \
  -o "$work/nonpreemptible.o"
"${gcc%-gcc}-readelf" -Wr "$work/nonpreemptible.o" \
  >"$work/nonpreemptible.reloc"
grep -q 'R_RISCV_PCREL_HI20' "$work/nonpreemptible.reloc"
for symbol in ordinary_extern ordinary_function; do
  grep -Eq "R_RISCV_GOT_HI20.*$symbol" "$work/nonpreemptible.reloc" || {
    echo "RV64_PIC_GLIBC_GOT=FAIL missing preemptible relocation for $symbol" >&2
    exit 1
  }
done
if grep -Eq 'R_RISCV_GOT_HI20.*(hidden_data|hidden_function|local_data|local_function|\.Lminic_string_)' \
    "$work/nonpreemptible.reloc"; then
  echo 'RV64_PIC_GLIBC_GOT=FAIL nonpreemptible GOT relocation' >&2
  exit 1
fi
"$gcc" -c -x assembler "$work/kernel.s" -o "$work/kernel.o"
"${gcc%-gcc}-readelf" -Wr "$work/kernel.o" >"$work/kernel.reloc"
grep -q 'R_RISCV_PCREL_HI20.*optind' "$work/kernel.reloc"
"$gcc" -pie "$work/mini.o" -o "$work/mini"
set +e
"$qemu" -L "$sysroot" "$work/gnu" >"$work/gnu.out" 2>"$work/gnu.err"
gnu_rc=$?
"$qemu" -L "$sysroot" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ] ||
   ! cmp -s "$work/gnu.out" "$work/mini.out" ||
   ! cmp -s "$work/gnu.err" "$work/mini.err"; then
  echo "RV64_PIC_GLIBC_GOT=FAIL GNU_rc=$gnu_rc MiniC_rc=$mini_rc" >&2
  cat "$work/gnu.out" "$work/mini.out" "$work/mini.err" >&2
  exit 1
fi
echo 'RV64_PIC_GLIBC_GOT=PASS GNU_QEMU_byte_exact=1 R_RISCV_GOT_HI20_extern=1 static_kernel_PCREL=1'
