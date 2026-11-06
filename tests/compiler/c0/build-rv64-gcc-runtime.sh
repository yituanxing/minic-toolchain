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
if [[ ! -s "$archive" ]]; then
  echo "RV64_RUNTIME=FAIL missing_archive file=$archive" >&2
  exit 8
fi
# The two primitive GNU libgcc sources and one upstream compiler-rt wide
# multiply are individually needed by Kbuild's separate purgatory.ro -r link.
for object in "$runtime/muldi3.o" "$runtime/div.o" \
              "$runtime/softfp/multi3.o"; do
  if [[ ! -s "$object" ]]; then
    echo "RV64_RUNTIME=FAIL missing_purgatory_provider file=$object" >&2
    exit 8
  fi
done
: >"$out/rv64-runtime-objects.rsp"
while IFS= read -r object; do
  if ! attrs=$(riscv64-linux-gnu-readelf -A "$object"); then
    echo "RV64_RUNTIME=FAIL readelf_attributes file=$object" >&2
    exit 8
  fi
  if ! grep -Eq 'Tag_RISCV_arch:[[:space:]]+"rv64i' <<<"$attrs"; then
    echo "RV64_RUNTIME=FAIL wrong_ISA_attribute file=$object attrs=$attrs" >&2
    exit 8
  fi
  printf '%s\n' "$object" >>"$out/rv64-runtime-objects.rsp"
done < <(find "$runtime" -type f -name '*.o' -print | LC_ALL=C sort)
object_count=$(wc -l < "$out/rv64-runtime-objects.rsp")
if ((object_count < 15)); then
  echo "RV64_RUNTIME=FAIL missing_object_inventory file=$out/rv64-runtime-objects.rsp count=$object_count" >&2
  exit 8
fi
# EF_RISCV_FLOAT_ABI_SOFT is numerically ZERO. GNU readelf correctly prints
# `Flags: 0x0` without necessarily spelling out `soft-float ABI`, therefore
# parse the actual ELF64 header rather than matching optional prose. Reject
# RVC bit 0 and float ABI bits 1-2; only base-I/LP64 objects qualify.
python3 - "$out/rv64-runtime-objects.rsp" <<'PY_ELF'
from pathlib import Path
import struct
import sys

for object_name in Path(sys.argv[1]).read_text().splitlines():
    data = Path(object_name).read_bytes()
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise SystemExit('RV64_RUNTIME=FAIL not_little_endian_ELF64 file=' + object_name)
    if struct.unpack_from('<H', data, 18)[0] != 243:
        raise SystemExit('RV64_RUNTIME=FAIL not_EM_RISCV file=' + object_name)
    flags = struct.unpack_from('<I', data, 48)[0]
    if flags & 0x7:
        raise SystemExit(f'RV64_RUNTIME=FAIL not_RV64I_LP64 file={object_name} e_flags={flags:#x}')
PY_ELF
if ! riscv64-linux-gnu-objdump -d "$archive" >"$out/rv64-runtime.dis"; then
  echo "RV64_RUNTIME=FAIL objdump_archive file=$archive" >&2
  exit 8
fi
if grep -Eq '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+[[:xdigit:]]+[[:space:]]+(mul|mulw|mulh|mulhu|mulhsu|div|divu|divw|divuw|rem|remu|remw|remuw)[[:space:]]' "$out/rv64-runtime.dis"; then
  echo 'RV64_RUNTIME=FAIL unexpected_M_extension' >&2
  exit 8
fi
echo "RV64_RUNTIME=PASS provider=existing_Makefile archive=$archive march=rv64i mabi=lp64 objects=$(wc -l < "$out/rv64-runtime-objects.rsp") sha256=$(sha256sum "$archive" | cut -d' ' -f1)"
