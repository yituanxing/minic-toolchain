#!/usr/bin/env bash
# One REAL MiniC-generated RISC-V Linux translation unit assembled with
# MiniAS vs GNU AS. Exact immutable .s input from P1-passing 2064-object
# CI #38426379946. No Linux C compilation, GNU golden cache, Kbuild or QEMU.
# Both assemblers must receive IDENTICAL -march=rv64gc -mabi=lp64d;
# otherwise test code silently creates incorrect MiniAS ELF e_flags=0.
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
timeout 75s "$minias" -march=rv64gc -mabi=lp64d \
  -o "$ev/minias.o" "$asm" \
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
for who in gnu minias; do
  cut -d' ' -f1 "$ev/$who.exports-sorted.txt" | LC_ALL=C sort -u \
    >"$ev/$who.export-names.txt"
done
if ! cmp -s "$ev/gnu.export-names.txt" "$ev/minias.export-names.txt"; then
  diff -u "$ev/gnu.export-names.txt" "$ev/minias.export-names.txt" \
    >"$ev/export-names-diff.txt" || true
  echo "MINIAS_EARLYCON=GAP missing_or_extra_exported_symbols" >&2
  head -n 70 "$ev/export-names-diff.txt" >&2
  exit 8
fi
echo "MINIAS_EARLYCON_SYMBOL_NAMES=PASS same_defined_global_symbols=true"
if ! cmp -s "$ev/gnu.exports-sorted.txt" "$ev/minias.exports-sorted.txt"; then
  diff -u "$ev/gnu.exports-sorted.txt" "$ev/minias.exports-sorted.txt" \
    >"$ev/export-address-size-diff.txt" || true
  echo "MINIAS_EARLYCON_LAYOUT=DIFF exported_symbol_offsets_or_sizes"
fi
if ! cmp -s "$ev/gnu.text.bin" "$ev/minias.text.bin"; then
  echo "MINIAS_EARLYCON_TEXT=DIFF GNU_RVC_auto_compression_or_instruction_encoding"
  sha256sum "$ev/"*.text.bin
  # Diagnostic only: disable C in BOTH assemblers on the same immutable .s,
  # distinguishing GNU's automatic RVC encoding from other semantic gaps.
  rvc_gnu=0
  riscv64-linux-gnu-as -march=rv64g -mabi=lp64d \
    -o "$ev/gnu-no-rvc.o" "$asm" >"$ev/gnu-no-rvc.stdout" \
    2>"$ev/gnu-no-rvc.stderr" || rvc_gnu=$?
  rvc_mini=0
  timeout 75s "$minias" -march=rv64g -mabi=lp64d \
    -o "$ev/minias-no-rvc.o" "$asm" >"$ev/minias-no-rvc.stdout" \
    2>"$ev/minias-no-rvc.stderr" || rvc_mini=$?
  echo "MINIAS_EARLYCON_NO_RVC_PROBE gnu_rc=$rvc_gnu minias_rc=$rvc_mini"
  if [[ "$rvc_gnu" == 0 && "$rvc_mini" == 0 ]]; then
    riscv64-linux-gnu-objcopy -j .text -O binary \
      "$ev/gnu-no-rvc.o" "$ev/gnu-no-rvc.text.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
      "$ev/minias-no-rvc.o" "$ev/minias-no-rvc.text.bin"
    if cmp -s "$ev/gnu-no-rvc.text.bin" "$ev/minias-no-rvc.text.bin"; then
      echo "MINIAS_EARLYCON_NO_RVC=PASS text_byte_exact_without_C=true"
    else
      echo "MINIAS_EARLYCON_NO_RVC=DIFF other_instruction_or_layout_gap"
      wc -c "$ev/"*-no-rvc.text.bin
      sha256sum "$ev/"*-no-rvc.text.bin
      riscv64-linux-gnu-objdump -dr -M no-aliases "$ev/gnu-no-rvc.o" \
        >"$ev/gnu-no-rvc.disasm.txt"
      riscv64-linux-gnu-objdump -dr -M no-aliases "$ev/minias-no-rvc.o" \
        >"$ev/minias-no-rvc.disasm.txt"
      python3 - "$ev/gnu-no-rvc.text.bin" "$ev/minias-no-rvc.text.bin" <<'PY_DIFF'
from pathlib import Path
import difflib,sys
a,b=[Path(p).read_bytes() for p in sys.argv[1:]]
first=next((i for i in range(min(len(a),len(b))) if a[i]!=b[i]), min(len(a),len(b)))
print(f"MINIAS_EARLYCON_FIRST_TEXT_DIFF offset=0x{first:x} gnu_bytes={len(a)} minias_bytes={len(b)}")
print(f"MINIAS_EARLYCON_GNU_WINDOW={a[max(0,first-24):first+32].hex(' ')}")
print(f"MINIAS_EARLYCON_MINIAS_WINDOW={b[max(0,first-24):first+32].hex(' ')}")
PY_DIFF
      diff -u "$ev/gnu-no-rvc.disasm.txt" "$ev/minias-no-rvc.disasm.txt" \
        >"$ev/no-rvc-disassembly.diff.txt" || true
      echo "MINIAS_EARLYCON_DISASM_FIRST_DIFF"
      head -n 85 "$ev/no-rvc-disassembly.diff.txt"
    fi
  fi
  echo "MINIAS_EARLYCON=GAP rv64gc_text_not_GNU_equivalent_not_safe_for_Linux_P1" >&2
  exit 8
fi
echo "MINIAS_EARLYCON=PASS source=immutable_MiniC_earlycon_s abi=GNU_same symbols=GNU_same text_bytes=GNU_same full_relocations_not_yet_certified=true linux_C_compiles=0 no_runtime_claim=true"
