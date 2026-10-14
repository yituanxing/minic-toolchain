#!/usr/bin/env bash
# One REAL MiniC-generated RISC-V Linux translation unit assembled with
# MiniAS vs GNU AS. Exact immutable .s input from P1-passing 2064-object
# CI #38426379946. No Linux C compilation, GNU golden cache, Kbuild or QEMU.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <downloaded-artifact-dir> <minias-build-dir> <evidence-dir>" >&2
  exit 64
fi
archive=$(realpath "$1")
toolchain=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
asm="$archive/trials/full_all/earlycon-source/earlycon-minic.s"
[[ -s "$asm" ]] || {
  echo "MINIAS_EARLYCON=INCONCLUSIVE immutable_assembly_missing:$asm" >&2
  exit 8
}
printf '%s  %s\n' \
  5a2d6035541018dbacbcc8b7a838f24239d934fd3c3b7fdb3e414e23ddacc02b \
  "$asm" | sha256sum -c -
sha256sum "$asm" >"$ev/assembly-input.sha256"
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" \
  "$toolchain/bin/minic-as"
minias="$toolchain/bin/minic-as"
sha256sum "$minias" >"$ev/mini-as-binary.sha256"
start=$(date +%s%3N)
riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d -o "$ev/gnu.o" "$asm" \
  >"$ev/gnu.stdout" 2>"$ev/gnu.stderr"
echo "MINIAS_EARLYCON_GNU_AS=PASS elapsed_ms=$(($(date +%s%3N)-start))"
start=$(date +%s%3N)
status=0
timeout 75s "$minias" -o "$ev/minias.o" "$asm" \
  >"$ev/minias.stdout" 2>"$ev/minias.stderr" || status=$?
echo "MINIAS_EARLYCON_NATIVE_AS elapsed_ms=$(($(date +%s%3N)-start)) rc=$status"
if (( status != 0 )); then
  echo "MINIAS_EARLYCON=GAP native_assembler_first_failure" >&2
  head -n 25 "$ev/minias.stderr" >&2
  exit "$status"
fi
for who in gnu minias; do
  riscv64-linux-gnu-readelf -Wh "$ev/$who.o" >"$ev/$who.elf-header.txt"
  riscv64-linux-gnu-readelf -W -S "$ev/$who.o" >"$ev/$who.sections.txt"
  riscv64-linux-gnu-readelf -W -r "$ev/$who.o" >"$ev/$who.relocations.txt"
  riscv64-linux-gnu-nm -g --defined-only -P "$ev/$who.o" >"$ev/$who.exports.txt"
  LC_ALL=C sort -u "$ev/$who.exports.txt" >"$ev/$who.exports-sorted.txt"
  riscv64-linux-gnu-objcopy -j .text -O binary "$ev/$who.o" "$ev/$who.text.bin"
  sha256sum "$ev/$who.o" "$ev/$who.text.bin" >"$ev/$who.hashes.txt"
done
python3 - "$ev/gnu.o" "$ev/minias.o" <<'PY_ELF'
from pathlib import Path
import struct,sys
g,m=[Path(p).read_bytes() for p in sys.argv[1:]]
for p,b in zip(sys.argv[1:],(g,m)):
    assert len(b)>=64 and b[:6]==b'\x7fELF\x02\x01', f'ELF64LE required:{p}'
    assert struct.unpack_from('<H',b,16)[0]==1, f'ET_REL required:{p}'
    assert struct.unpack_from('<H',b,18)[0]==243, f'EM_RISCV required:{p}'
assert g[48:52]==m[48:52], 'ABI e_flags differs'
print(f'MINIAS_EARLYCON_ELF=PASS rv64_ET_REL=true abi_flags={g[48:52].hex()}')
PY_ELF
if ! cmp -s "$ev/gnu.exports-sorted.txt" "$ev/minias.exports-sorted.txt"; then
  diff -u "$ev/gnu.exports-sorted.txt" "$ev/minias.exports-sorted.txt" \
    >"$ev/exports-diff.txt" || true
  echo "MINIAS_EARLYCON=GAP exports_differ" >&2
  head -n 70 "$ev/exports-diff.txt" >&2
  exit 8
fi
if ! cmp -s "$ev/gnu.text.bin" "$ev/minias.text.bin"; then
  echo "MINIAS_EARLYCON=GAP text_opcode_bytes_differ" >&2
  sha256sum "$ev/"*.text.bin >&2
  exit 8
fi
echo "MINIAS_EARLYCON=PASS source=immutable_MiniC_earlycon_s abi=GNU_same symbols=GNU_same text_bytes=GNU_same full_relocations_not_yet_certified=true linux_C_compiles=0 no_runtime_claim=true"
