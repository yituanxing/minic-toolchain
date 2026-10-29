#!/usr/bin/env bash
# Keep C's real source va_list object separate from the private ABI cursor.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${1:?usage: probe-va-cursor-source-object.sh /absolute/path/to/minic-cc}
case "$minic" in
  /*) ;;
  *) echo 'MiniC path must be absolute' >&2; exit 2 ;;
esac
test -x "$minic"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

for fixture in x86_va_cursor_source_basic_runtime x86_va_cursor_source_object_runtime; do
  source="$root/tests/compiler/c0/$fixture.c"
  cc -E -P -x c "$source" -o "$work/$fixture.i"
  cc -std=gnu11 -O0 -Wall -Wextra -Werror "$source" \
    -o "$work/$fixture.gnu"
  "$work/$fixture.gnu"
  "$minic" --target=x86_64-linux -S "$work/$fixture.i" \
    -o "$work/$fixture.s"
  cc -O0 -no-pie "$work/$fixture.s" -o "$work/$fixture.mini"
  "$work/$fixture.mini"
done

# AAPCS64 source is a 32-byte struct; the NEXT address and COPY transfer
# must remain full-width even though the host GCC cannot run AArch64 code.
aarch_source="$root/tests/compiler/c0/aarch64_va_cursor_source_object_runtime.c"
cc -E -P -x c "$aarch_source" -o "$work/aarch64-source.i"
"$minic" --target=aarch64-linux -S "$work/aarch64-source.i" \
  -o "$work/aarch64-source.s"
grep -F 'sum_after_copy:' "$work/aarch64-source.s" >/dev/null
grep -F 'ldp x11, x12, [x10]' "$work/aarch64-source.s" >/dev/null
grep -F 'ldp x13, x14, [x10, #16]' "$work/aarch64-source.s" >/dev/null
grep -F 'stp x11, x12, [x9]' "$work/aarch64-source.s" >/dev/null
grep -F 'stp x13, x14, [x9, #16]' "$work/aarch64-source.s" >/dev/null

# The structured source ABI cannot accept the historic char* proxy. Keep
# an explicit negative on both targets; the linear RV/Loong ABIs still use
# their historical scalar-pointer cursor source representation.
for target in x86_64-linux aarch64-linux; do
  if "$minic" --target="$target" -S \
      "$root/tests/compiler/c0/builtin_va_copy.c" \
      -o "$work/$target-invalid.s" >"$work/$target.out" \
      2>"$work/$target.err"; then
    echo "FAIL: legacy pointer va_list accepted by $target" >&2
    exit 1
  fi
  grep -F 'modifiable va_list lvalue' "$work/$target.err" >/dev/null
  test ! -s "$work/$target-invalid.s"
done
for target in riscv32-linux riscv64-linux loongarch64-linux; do
  "$minic" --target="$target" -S \
    "$root/tests/compiler/c0/builtin_va_copy.c" \
    -o "$work/$target-linear.s"
done

qemu_status=NOT_RUN
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-aarch64 >/dev/null 2>&1; then
  aarch64-linux-gnu-gcc -std=gnu11 -O0 -static "$aarch_source" \
    -o "$work/aarch64-source.gnu"
  aarch64-linux-gnu-gcc -static -x assembler "$work/aarch64-source.s" \
    -o "$work/aarch64-source.mini"
  timeout 30s qemu-aarch64 "$work/aarch64-source.gnu"
  timeout 30s qemu-aarch64 "$work/aarch64-source.mini"
  qemu_status=PASS
fi

echo "MINIC_VA_SOURCE_OBJECT=PASS x86_gcc_native=2 x86_minic_native=2 aarch64_source_32B_S=1 structured_pointer_proxy_rejected=2 linear_pointer_legacy_S=3 GNU_QEMU=$qemu_status"
