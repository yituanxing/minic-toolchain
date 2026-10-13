#!/usr/bin/env bash
# Reuse frozen GNU Linux Kbuild cache; test ONLY the existing purgatory -r
# boundary with native MiniLD. No MiniC/GCC C compile, Kbuild, or QEMU.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <gnu-kbuild-out> <minild-tool-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
toolchain=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
files=(
  arch/riscv/purgatory/purgatory.o
  arch/riscv/purgatory/sha256.o
  arch/riscv/purgatory/entry.o
  arch/riscv/purgatory/string.o
  arch/riscv/purgatory/ctype.o
  arch/riscv/purgatory/memcpy.o
  arch/riscv/purgatory/memset.o
  arch/riscv/purgatory/strcmp.o
  arch/riscv/purgatory/strlen.o
  arch/riscv/purgatory/strncmp.o
)
for f in "${files[@]}" arch/riscv/purgatory/purgatory.ro; do
  [[ -s "$out/$f" ]] || {
    echo "MINILD_PURGATORY_R=INCONCLUSIVE missing_GNU_cache=$f" >&2
    exit 8
  }
  sha256sum "$out/$f" >>"$ev/input-sha256.txt"
done
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain"   "$toolchain/bin/minic-ld"
minild="$toolchain/bin/minic-ld"
sha256sum "$minild" >"$ev/tool-sha256.txt"
(
  cd "$out"
  start=$(date +%s%3N)
  status=0
  timeout 90s "$minild" -melf64lriscv -z noexecstack     --no-warn-rwx-segments -r -e purgatory_start -z nodefaultlib     "${files[@]}" -o "$ev/purgatory.ro"     >"$ev/purgatory.stdout" 2>"$ev/purgatory.stderr" || status=$?
  elapsed=$(($(date +%s%3N) - start))
  printf '%s\t%s\n' "$elapsed" "$status" >"$ev/timing.tsv"
  echo "MINILD_PURGATORY_R_TIMING elapsed_ms=$elapsed rc=$status linux_C_compiles=0"
  if (( status != 0 )); then
    tail -n 70 "$ev/purgatory.stderr" >&2
    exit "$status"
  fi
)
python3 - "$out/arch/riscv/purgatory/purgatory.ro" "$ev/purgatory.ro" <<'PYABI'
import struct,sys
from pathlib import Path
for p in map(Path,sys.argv[1:]):
    h=p.read_bytes()[:64]
    assert h[:6]==b'\x7fELF\x02\x01', 'ELF64LE'
    assert struct.unpack_from('<H',h,16)[0]==1, 'ET_REL'
    assert struct.unpack_from('<H',h,18)[0]==243, 'RISC-V'
    print(f'MINILD_PURGATORY_ELF=PASS {p.name} riscv_elf64_relocatable')
p,q=[Path(x).read_bytes()[:52] for x in sys.argv[1:]]
assert p[48:52]==q[48:52], 'RISC-V ABI flags differ'
print('MINILD_PURGATORY_ABI=PASS same_GNU_flags=true')
PYABI
riscv64-linux-gnu-nm --defined-only -g -P "$out/arch/riscv/purgatory/purgatory.ro" >"$ev/gnu-nm.txt"
riscv64-linux-gnu-nm --defined-only -g -P "$ev/purgatory.ro" >"$ev/minild-nm.txt"
cut -d' ' -f1 "$ev/gnu-nm.txt" | LC_ALL=C sort -u >"$ev/gnu-names.txt"
cut -d' ' -f1 "$ev/minild-nm.txt" | LC_ALL=C sort -u >"$ev/minild-names.txt"
if ! cmp -s "$ev/gnu-names.txt" "$ev/minild-names.txt"; then
  diff -u "$ev/gnu-names.txt" "$ev/minild-names.txt" >"$ev/diff.txt" || true
  echo "MINILD_PURGATORY_R=INCONCLUSIVE exported_symbol_mismatch" >&2
  head -n 50 "$ev/diff.txt" >&2
  exit 8
fi
sha256sum "$ev/purgatory.ro" >"$ev/output-sha256.txt"
echo "MINILD_PURGATORY_R=PASS real_GNU_linux_purgatory_r=true same_ABI=true same_global_names=true no_Linux_C_rebuild=true runtime_claim=false"
