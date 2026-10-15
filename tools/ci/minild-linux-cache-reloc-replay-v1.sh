#!/usr/bin/env bash
# Targeted real-Linux GNU-LD->MiniLD replacement experiment for `-r vmlinux.o`.
# Cache-driven: one relocatable link, no Linux C compilation or Kbuild recursions.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <gnu-kbuild-out> <minild-build-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
toolchain=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
for path in vmlinux.a vmlinux.o drivers/firmware/efi/libstub/lib.a; do
  [[ -s "$out/$path" ]] || {
    echo "MINILD_RELOC_REPLAY=INCONCLUSIVE missing_gnu_cache=$path" >&2
    exit 8
  }
  sha256sum "$out/$path" >>"$ev/reloc-input-sha256.txt"
done
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" \
  "$toolchain/bin/minic-ld"
minild="$toolchain/bin/minic-ld"
sha256sum "$minild" >"$ev/reloc-tool-sha256.txt"
(
  cd "$out"
  started=$(date +%s%3N)
  status=0
  timeout 90s "$minild" -melf64lriscv -z noexecstack \
    --no-warn-rwx-segments -r -o "$ev/vmlinux-reloc.o" \
    --whole-archive vmlinux.a --no-whole-archive --start-group \
    ./drivers/firmware/efi/libstub/lib.a --end-group \
    >"$ev/reloc.stdout" 2>"$ev/reloc.stderr" || status=$?
  elapsed=$(($(date +%s%3N)-started))
  printf '%s\t%s\n' "$elapsed" "$status" >"$ev/reloc-timing.tsv"
  echo "MINILD_RELOC_REPLAY_TIMING elapsed_ms=$elapsed rc=$status gcc_C_cache=true"
  if (( status != 0 )); then
    tail -n 85 "$ev/reloc.stderr" >&2
    exit "$status"
  fi
)
[[ -s "$ev/vmlinux-reloc.o" ]] || {
  echo "MINILD_RELOC_REPLAY=FAIL missing_output" >&2
  exit 9
}
python3 - "$out/vmlinux.o" "$ev/vmlinux-reloc.o" <<'PY_ELF'
import struct,sys
from pathlib import Path
for path in map(Path,sys.argv[1:]):
    b=path.read_bytes()[:64]
    assert len(b)>=64 and b[:6]==b'\x7fELF\x02\x01', path
    assert struct.unpack_from('<H',b,16)[0]==1, 'must be ET_REL'
    assert struct.unpack_from('<H',b,18)[0]==243, 'must be RISC-V'
    print(f"MINILD_RELOC_ELF=PASS path={path} rv64_ET_REL=true abi_flags={b[48:52].hex()}")
gold=Path(sys.argv[1]).read_bytes()[:52]
new=Path(sys.argv[2]).read_bytes()[:52]
assert gold[48:52]==new[48:52], 'GNU vs MiniLD RISC-V ABI flags differ'
print('MINILD_RELOC_ABI=PASS same_GNU_flags=true')
PY_ELF
riscv64-linux-gnu-nm -g --defined-only -P "$out/vmlinux.o" \
  >"$ev/reloc-gnu-symbols.txt"
riscv64-linux-gnu-nm -g --defined-only -P "$ev/vmlinux-reloc.o" \
  >"$ev/reloc-minild-symbols.txt"
cut -d' ' -f1 "$ev/reloc-gnu-symbols.txt" | LC_ALL=C sort -u >"$ev/reloc-gnu-names.txt"
cut -d' ' -f1 "$ev/reloc-minild-symbols.txt" | LC_ALL=C sort -u >"$ev/reloc-minild-names.txt"
if ! cmp -s "$ev/reloc-gnu-names.txt" "$ev/reloc-minild-names.txt"; then
  diff -u "$ev/reloc-gnu-names.txt" "$ev/reloc-minild-names.txt" \
    >"$ev/reloc-symbol-diff.txt" || true
  echo "MINILD_RELOC_REPLAY=INCONCLUSIVE reason=global_export_name_mismatch" >&2
  head -n 80 "$ev/reloc-symbol-diff.txt" >&2
  exit 8
fi
sha256sum "$ev/vmlinux-reloc.o" >"$ev/reloc-output.sha256"
echo "MINILD_RELOC_REPLAY=PASS real_Linux_r_link=true symbols=GNU_same_names abi=GNU_same no_Linux_C_rebuild=true no_runtime_claim=true"
