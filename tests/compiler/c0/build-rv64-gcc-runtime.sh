#!/usr/bin/env bash
# CI adapter to the project's existing, pinned RV64I runtime Makefile target.
# No separate runtime inventory or GCC implementation is maintained here.
set -Eeuo pipefail
out="${1:?usage: build-rv64-gcc-runtime.sh WORKDIR}"
out=$(realpath -m "$out")
root=$(git rev-parse --show-toplevel)
mkdir -p "$out"
build="$out/build"
make -s -C "$root" BUILD_DIR="$build" \
  RISCV_RUNTIME_CC="${RISCV_GCC:-riscv64-linux-gnu-gcc}" \
  RISCV_RUNTIME_AR=riscv64-linux-gnu-ar \
  RISCV_RUNTIME_NM=riscv64-linux-gnu-nm \
  check-rv64i-runtime-archive
runtime="$build/runtime/riscv64/rv64i-lp64"
archive="$runtime/libminic-rt.a"
test -s "$archive"
# The two primitive GNU libgcc sources and one upstream compiler-rt wide
# multiply are individually needed by Kbuild's separate purgatory.ro -r link.
for object in "$runtime/muldi3.o" "$runtime/div.o" \
              "$runtime/softfp/multi3.o"; do
  test -s "$object"
done
: >"$out/rv64-runtime-objects.rsp"
while IFS= read -r object; do
  riscv64-linux-gnu-readelf -h "$object" | grep -Eq 'Class:[[:space:]]+ELF64'
  riscv64-linux-gnu-readelf -h "$object" | grep -Eq 'Machine:[[:space:]]+RISC-V'
  riscv64-linux-gnu-readelf -h "$object" | grep -Eq 'Flags:.*soft-float ABI'
  riscv64-linux-gnu-readelf -A "$object" | grep -Eq 'Tag_RISCV_arch:[[:space:]]+"rv64i'
  printf '%s\n' "$object" >>"$out/rv64-runtime-objects.rsp"
done < <(find "$runtime" -type f -name '*.o' -print | LC_ALL=C sort)
test "$(wc -l < "$out/rv64-runtime-objects.rsp")" -ge 15
riscv64-linux-gnu-objdump -d "$archive" >"$out/rv64-runtime.dis"
if grep -Eq '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+[[:xdigit:]]+[[:space:]]+(mul|mulw|mulh|mulhu|mulhsu|div|divu|divw|divuw|rem|remu|remw|remuw)[[:space:]]' "$out/rv64-runtime.dis"; then
  echo 'RV64_RUNTIME=FAIL unexpected_M_extension' >&2
  exit 8
fi
echo "RV64_RUNTIME=PASS provider=existing_Makefile archive=$archive march=rv64i mabi=lp64 objects=$(wc -l < "$out/rv64-runtime-objects.rsp") sha256=$(sha256sum "$archive" | cut -d' ' -f1)"
