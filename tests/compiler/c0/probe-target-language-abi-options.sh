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
cat >"$work/wchar16.i" <<'SRC'
_Static_assert(sizeof(L"X") == 4, "two 16-bit wchar units");
int model(void) { return sizeof(L"X"); }
SRC
cat >"$work/wchar32.i" <<'SRC'
_Static_assert(sizeof(L"X") == 8, "two 32-bit wchar units");
int model(void) { return sizeof(L"X"); }
SRC
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
  # RV32 machine emission must honor plain-char signedness, not merely
  # accept the target option or agree with the driver frontend output.
  if [[ "$arch" == riscv32-linux ]]; then
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
  # Last lexical option wins, as in GCC/Clang.
  "$MINIC_CC" "--target=$arch" "${extra[@]}" \
    -fsigned-char -funsigned-char -S "$work/unsigned.i" -o "$work/last.s"
  "$MINIC" "--target=$arch" "${extra[@]}" \
    -fsigned-char -funsigned-char -S "$work/unsigned.i" -o "$work/last-driver.s"
  cmp "$work/last.s" "$work/last-driver.s"
done
for profile in 'rv32i ilp32 riscv32-linux' 'rv64i lp64 riscv64-linux' \
  'rv64im lp64 riscv64-linux' 'rv64imafd lp64d riscv64-linux'; do
  read -r march abi arch <<< "$profile"
  "$MINIC_CC" "--target=$arch" "-march=$march" "-mabi=$abi" \
    -S "$work/unsigned.i" -funsigned-char -o "$work/profile-cc.s"
  "$MINIC" "--target=$arch" "-march=$march" "-mabi=$abi" \
    -S "$work/unsigned.i" -funsigned-char -o "$work/profile-driver.s"
  cmp "$work/profile-cc.s" "$work/profile-driver.s"
done
for bad in 'rv32i lp64 riscv32-linux' 'rv64i lp64d riscv64-linux' \
  'rv64im ilp32 riscv64-linux' 'rv64imafd lp64 riscv64-linux'; do
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
if "$MINIC" --target=x86_64-linux -fPIC -S "$work/signed.i" \
  -o "$work/bad-pic.s" 2>"$work/bad-pic.err"; then
  echo 'MINIC_TARGET_ABI_FLAGS=FAIL unqualified PIC' >&2; exit 1
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
echo 'MINIC_TARGET_ABI_FLAGS=PASS five_targets char_signedness wchar_override target_isolation PIC_fail_closed'
