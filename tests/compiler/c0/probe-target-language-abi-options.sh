#!/usr/bin/env bash
# C language ABI options are a translation-unit policy, independent of
# -march/-mabi, machine XLEN and the assembler/linker toolchain. No QEMU.
set -Eeuo pipefail
: "${MINIC:?provide staged MiniC driver}"
: "${MINIC_CC:?provide minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/signed.i" <<'SRC'
_Static_assert((char)-1 < 0, "signed plain char");
int model(void) { char x = -1; return x; }
SRC
cat >"$work/unsigned.i" <<'SRC'
_Static_assert((char)-1 > 0, "unsigned plain char");
int model(void) { char x = -1; return x; }
SRC
cat >"$work/volatile-char.i" <<'SRC'
volatile char value;
int model(void) { return value; }
SRC
cat >"$work/rv32-float.i" <<'SRC'
float add(float x, float y) { return x + y; }
float subtract(float x, float y) { return x - y; }
float multiply(float x, float y) { return x * y; }
float divide(float x, float y) { return x / y; }
SRC
cat >"$work/wchar16.i" <<'SRC'
_Static_assert(sizeof(L"X") == 4, "two 16-bit wchar units");
int model(void) { return sizeof(L"X"); }
SRC
cat >"$work/wchar32.i" <<'SRC'
_Static_assert(sizeof(L"X") == 8, "two 32-bit wchar units");
int model(void) { return sizeof(L"X"); }
SRC
# GNU Linux 6.6 Kbuild universally sets -fshort-wchar because EFI uses
# 16-bit efi_char16_t; an L"..." expression is *not* rewritten by the
# preprocessor. The MiniC replay must carry the original Kbuild flag.
cat >"$work/efi-getvar.i" <<'SRC'
typedef unsigned short efi_char16_t;
typedef struct { unsigned int data; } efi_guid_t;
typedef unsigned long (*efi_get_var_t)(efi_char16_t *,
                                       const efi_guid_t *, void *,
                                       unsigned long *, void *);
unsigned long secure_boot(efi_get_var_t get_var) {
    unsigned long size = 1;
    return get_var(L"SecureBoot", &(efi_guid_t){ 0 }, 0, &size, 0);
}
SRC
# Linux KBUILD_CFLAGS sets -fshort-wchar: a frozen .i alone omits this
# source-language ABI choice, which EFI indirect calls require.
cat >"$work/efi-get-variable.i" <<'SRC'
typedef unsigned short efi_char16_t;
typedef unsigned long (*efi_get_variable_t)(efi_char16_t *);
static efi_get_variable_t get_var;
unsigned long read_secure_boot(void) {
    return get_var(L"SecureBoot");
}
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -fshort-wchar -S "$work/efi-get-variable.i" -o "$work/efi-cc.s"
"$MINIC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -fshort-wchar -S "$work/efi-get-variable.i" -o "$work/efi-driver.s"
cmp "$work/efi-cc.s" "$work/efi-driver.s"
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -fno-short-wchar -S "$work/efi-get-variable.i" -o "$work/efi-bad.s" \
  2>"$work/efi-bad.err"; then
  echo 'MINIC_TARGET_ABI_FLAGS=FAIL: EFI UTF-16 accepted under wchar32' >&2
  exit 1
fi
grep -Fq 'indirect call argument type does not match declaration' "$work/efi-bad.err"
test ! -e "$work/efi-bad.s"

for arch in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
  extra=()
  case "$arch" in
    riscv32-linux) extra=(-march=rv32i -mabi=ilp32) ;;
    riscv64-linux) extra=(-march=rv64i -mabi=lp64) ;;
  esac
  for option in -fsigned-char -fno-unsigned-char; do
    "$MINIC_CC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/signed.i" -o "$work/cc.s"
    "$MINIC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/signed.i" -o "$work/driver.s"
    cmp "$work/cc.s" "$work/driver.s"
  done
  for option in -funsigned-char -fno-signed-char; do
    "$MINIC_CC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/unsigned.i" -o "$work/cc.s"
    "$MINIC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/unsigned.i" -o "$work/driver.s"
    cmp "$work/cc.s" "$work/driver.s"
  done
  # Frozen GNU Kbuild .cmd files use both lower- and upper-case spellings.
  # The staged driver must preserve the same relocation policy as minic-cc;
  # dropping -fno-PIE here blocks strict all-owner Linux replacement.
  for option in -fno-pie -fno-PIE -fno-pic -fno-PIC; do
    "$MINIC_CC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/volatile-char.i" -o "$work/cc.s"
    "$MINIC" "--target=$arch" "${extra[@]}" "$option" \
      -S "$work/volatile-char.i" -o "$work/driver.s"
    cmp "$work/cc.s" "$work/driver.s"
  done
  "$MINIC_CC" "--target=$arch" "${extra[@]}" -fshort-wchar \
    -S "$work/wchar16.i" -o "$work/wchar16.s"
  "$MINIC" "--target=$arch" "${extra[@]}" -fshort-wchar \
    -S "$work/wchar16.i" -o "$work/driver16.s"
  cmp "$work/wchar16.s" "$work/driver16.s"
  normal="$work/wchar32.i"
  "$MINIC_CC" "--target=$arch" "${extra[@]}" -fno-short-wchar \
    -S "$normal" -o "$work/wchar-default.s"
  "$MINIC" "--target=$arch" "${extra[@]}" -fno-short-wchar \
    -S "$normal" -o "$work/driver-default.s"
  cmp "$work/wchar-default.s" "$work/driver-default.s"
  # RV32 machine emission: verify Base-I softfloat calls and plain-char
  # signedness, not merely frontend option acceptance.
  if [[ "$arch" == riscv32-linux ]]; then
    "$MINIC_CC" --target=riscv32-linux "${extra[@]}" \
      -S "$work/rv32-float.i" -o "$work/rv32-float.s"
    for sf_helper in __addsf3 __subsf3 __mulsf3 __divsf3; do
      grep -Fq "call $sf_helper" "$work/rv32-float.s" || {
        echo "MINIC_TARGET_ABI_FLAGS=FAIL rv32-softfloat-helper:$sf_helper" >&2
        exit 1
      }
    done
    if grep -Eq '^[[:space:]]+(fadd\.s|fsub\.s|fmul\.s|fdiv\.s)[[:space:]]' \
         "$work/rv32-float.s"; then
      echo 'MINIC_TARGET_ABI_FLAGS=FAIL rv32i-emitted-F-instructions' >&2
      exit 1
    fi
    "$MINIC_CC" --target=riscv32-linux "${extra[@]}" -fsigned-char \
      -S "$work/volatile-char.i" -o "$work/rv32-signed-machine.s"
    "$MINIC_CC" --target=riscv32-linux "${extra[@]}" -funsigned-char \
      -S "$work/volatile-char.i" -o "$work/rv32-unsigned-machine.s"
    grep -Eq '^[[:space:]]+lb[[:space:]]' "$work/rv32-signed-machine.s" || {
      echo 'MINIC_TARGET_ABI_FLAGS=FAIL rv32-signed-char-machine-missing-lb' >&2
      exit 1
    }
    grep -Eq '^[[:space:]]+lbu[[:space:]]' "$work/rv32-unsigned-machine.s" || {
      echo 'MINIC_TARGET_ABI_FLAGS=FAIL rv32-unsigned-char-machine-missing-lbu' >&2
      exit 1
    }
  fi
  if [[ "$arch" == riscv64-linux ]]; then
    "$MINIC_CC" "--target=$arch" "${extra[@]}" -fshort-wchar \
      -S "$work/efi-getvar.i" -o "$work/efi-short-cc.s"
    "$MINIC" "--target=$arch" "${extra[@]}" -fshort-wchar \
      -S "$work/efi-getvar.i" -o "$work/efi-short-driver.s"
    cmp "$work/efi-short-cc.s" "$work/efi-short-driver.s"
    # Without Kbuild's override the Linux EFI call is truly incompatible.
    # Rejecting it is correct; hiding the problem with a permissive
    # function-pointer conversion would create the wrong ABI.
    if "$MINIC_CC" "--target=$arch" "${extra[@]}" -fno-short-wchar \
        -S "$work/efi-getvar.i" -o "$work/efi-invalid.s" \
        >"$work/efi-invalid.out" 2>"$work/efi-invalid.err"; then
      echo 'MINIC_TARGET_ABI_FLAGS=FAIL EFI 32-bit wchar accepted without Kbuild flag' >&2
      exit 1
    fi
    grep -Fq 'indirect call argument type does not match declaration' \
      "$work/efi-invalid.err"
    echo 'MINIC_LINUX_EFI_CHAR16=PASS short_wchar=16 default_wchar=32'
  fi
  # Last lexical option wins, as in GCC/Clang.
  "$MINIC_CC" "--target=$arch" "${extra[@]}" \
    -fsigned-char -funsigned-char -S "$work/unsigned.i" -o "$work/last.s"
  "$MINIC" "--target=$arch" "${extra[@]}" \
    -fsigned-char -funsigned-char -S "$work/unsigned.i" -o "$work/last-driver.s"
  cmp "$work/last.s" "$work/last-driver.s"
done
# Linux/LoongArch64 Kbuild uses LP64S (-mabi=lp64s -msoft-float), while
# userland defaults to LP64D. The compiler must own the ABI choice; GNU AS
# merely annotating a default-LP64D object as LP64S is not an ABI proof.
cat >"$work/loong-lp64s-integer.i" <<'SRC'
extern double declared_but_unused(double);
struct floating_data_only { double value; };
static unsigned long counter;
unsigned long loong_kernel_integer(unsigned long x, unsigned long y) {
    counter += x;
    return counter ^ y ^ sizeof(struct floating_data_only);
}
SRC
"$MINIC_CC" --target=loongarch64-linux -mabi=lp64s \
  -S "$work/loong-lp64s-integer.i" -o "$work/loong-soft-cc.s"
"$MINIC" --target=loongarch64-linux -mabi=lp64s \
  -S "$work/loong-lp64s-integer.i" -o "$work/loong-soft-driver.s"
cmp "$work/loong-soft-cc.s" "$work/loong-soft-driver.s"
"$MINIC_CC" --target=loongarch64-linux -mabi=lp64d \
  -S "$work/loong-lp64s-integer.i" -o "$work/loong-double-explicit.s"
"$MINIC_CC" --target=loongarch64-linux \
  -S "$work/loong-lp64s-integer.i" -o "$work/loong-double-default.s"
cmp "$work/loong-double-explicit.s" "$work/loong-double-default.s"
cat >"$work/loong-lp64s-float.i" <<'SRC'
float loong_soft_not_implemented(float x, float y) { return x + y; }
SRC
cat >"$work/loong-lp64s-fp-record.i" <<'SRC'
struct mixed_float_int { double value; int tag; };
extern int consumes_mixed(struct mixed_float_int);
int pass_mixed(struct mixed_float_int item) { return consumes_mixed(item); }
SRC
for compiler in "$MINIC_CC" "$MINIC"; do
  for shape in float fp-record; do
    if "$compiler" --target=loongarch64-linux -mabi=lp64s \
        -S "$work/loong-lp64s-$shape.i" -o "$work/loong-soft-rejected.s" \
        2>"$work/loong-soft-rejected.err"; then
      echo "MINIC_TARGET_ABI_FLAGS=FAIL Loong LP64S emitted unsupported $shape ABI" >&2
      exit 1
    fi
    test ! -e "$work/loong-soft-rejected.s"
  done
done
for invalid in '-mabi=lp64f' '-mabi=lp64' '-march=loongarch64'; do
  if "$MINIC_CC" --target=loongarch64-linux -mabi=lp64s "$invalid" \
      -S "$work/loong-lp64s-integer.i" -o "$work/loong-bad-target.s" \
      2>"$work/loong-bad-target.err"; then
    echo "MINIC_TARGET_ABI_FLAGS=FAIL accepted unsupported Loong option:$invalid" >&2
    exit 1
  fi
  test ! -e "$work/loong-bad-target.s"
done
echo 'MINIC_LOONGARCH_LP64S=PASS kernel_integer=1 float_and_record_fail_closed=1 default_lp64d=1'
for profile in 'rv32i ilp32 riscv32-linux' 'rv64i lp64 riscv64-linux' \
  'rv64im lp64 riscv64-linux' 'rv64imafd lp64d riscv64-linux' \
  'rv64imafd lp64 riscv64-linux' \
  'rv64ima_zicsr_zifencei lp64 riscv64-linux' \
  'rv64imac_zicsr_zifencei lp64 riscv64-linux'; do
  read -r march abi arch <<< "$profile"
  "$MINIC_CC" "--target=$arch" "-march=$march" "-mabi=$abi" \
    -S "$work/unsigned.i" -funsigned-char -o "$work/profile-cc.s"
  "$MINIC" "--target=$arch" "-march=$march" "-mabi=$abi" \
    -S "$work/unsigned.i" -funsigned-char -o "$work/profile-driver.s"
  cmp "$work/profile-cc.s" "$work/profile-driver.s"
done
for bad in 'rv32i lp64 riscv32-linux' 'rv64i lp64d riscv64-linux' \
  'rv64im ilp32 riscv64-linux' \
  'rv64ima_zicsr_zifencei lp64d riscv64-linux'; do
  read -r march abi arch <<< "$bad"
  if "$MINIC_CC" "--target=$arch" "-march=$march" "-mabi=$abi" \
    -S "$work/unsigned.i" -o "$work/bad-abi.s" 2>"$work/bad-abi.err"; then
    echo "MINIC_TARGET_ABI_FLAGS=FAIL incompatible ISA/ABI:$bad" >&2
    exit 1
  fi
  test ! -e "$work/bad-abi.s"
done
"$MINIC_CC" --target=riscv64-linux -fPIC -S "$work/unsigned.i" \
  -o "$work/pic-cc.s" -funsigned-char
"$MINIC" --target=riscv64-linux -fPIC -S "$work/unsigned.i" \
  -o "$work/pic-driver.s" -funsigned-char
cmp "$work/pic-cc.s" "$work/pic-driver.s"
# Original Linux x86 PIC is now qualified by TargetInfo and independent
# GNU shared-library/PIE ABI proof (probe-x86-linux-pic.sh). Both frontends
# must forward identical -fPIC emission. AArch64 and LoongArch64 now have
# independently target-owned local/GOT PIC sequences as well.
"$MINIC_CC" --target=x86_64-linux -fPIC -S "$work/signed.i" \
  -o "$work/x86-pic-cc.s"
"$MINIC" --target=x86_64-linux -fPIC -S "$work/signed.i" \
  -o "$work/x86-pic-driver.s"
cmp "$work/x86-pic-cc.s" "$work/x86-pic-driver.s"
for target in aarch64 loongarch64; do
  for pic_flag in -fPIC -fPIE; do
    "$MINIC_CC" "--target=$target-linux" "$pic_flag" -fsigned-char \
      -S "$work/signed.i" \
      -o "$work/$target-pic-cc.s"
    "$MINIC" "--target=$target-linux" "$pic_flag" -fsigned-char \
      -S "$work/signed.i" \
      -o "$work/$target-pic-driver.s"
    cmp "$work/$target-pic-cc.s" "$work/$target-pic-driver.s"
  done
done
if "$MINIC" --target=riscv32-linux -fPIC -S "$work/signed.i" \
  -o "$work/bad-pic.s" 2>"$work/bad-pic.err"; then
  echo 'MINIC_TARGET_ABI_FLAGS=FAIL unqualified RV32 PIC' >&2; exit 1
fi
grep -Fq 'PIC lowering is not yet qualified' "$work/bad-pic.err"
test ! -e "$work/bad-pic.s"
for option in -mcmodel=large -fshort-enums -fwrapv -mtune=generic; do
  if "$MINIC_CC" --target=riscv64-linux "$option" -S "$work/unsigned.i" \
    -o "$work/unsupported.s" 2>"$work/unsupported.err"; then
    echo "MINIC_TARGET_ABI_FLAGS=FAIL unqualified:$option" >&2; exit 1
  fi
  test ! -e "$work/unsupported.s"
done
# AAPCS64 va_list is mutable 32-byte state. A source parameter and the
# destination cursor must be independent; emitting the 32-byte copy is
# target-owned, whereas Core preserves the unaliased cursor operation.
cat >"$work/aarch64-va-copy.i" <<'EOF'
int minic_aarch64_va_copy_accept(__builtin_va_list source) {
  __builtin_va_list independent;
  __builtin_va_copy(independent, source);
  __builtin_va_end(independent);
  return 0;
}
EOF
"$MINIC_CC" --target=aarch64-linux -S "$work/aarch64-va-copy.i" \
    -o "$work/aarch64-va-copy.s"
grep -Fq 'ldp x11, x12, [x10]' "$work/aarch64-va-copy.s"
grep -Fq 'stp x13, x14, [x9, #16]' "$work/aarch64-va-copy.s"
echo 'MINIC_AARCH64_VA_COPY=PASS independent_32byte_ABI_cursor_copy'
echo 'MINIC_TARGET_ABI_FLAGS=PASS five_targets char_signedness wchar_override target_isolation PIC_fail_closed'
