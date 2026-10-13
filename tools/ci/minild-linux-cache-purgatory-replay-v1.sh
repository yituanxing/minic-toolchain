#!/usr/bin/env bash
# Reuse frozen GNU Linux Kbuild cache; test ONLY the existing purgatory -r
# Purgatory isolated replay trigger uses no prior MiniC artifacts.
# Re-run exact GNU purgatory.chk exported-name oracle after native GNU-default
# linker-script PROVIDE symbols were added; no Linux C TU recompilation.
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

# Independently check the remaining GNU final purgatory.chk ET_EXEC link.
# Feed the original GNU purgatory.ro to isolate THIS operation; do not let
# differences in the newly generated -r object obscure diagnosis.
gnu_ro="$out/arch/riscv/purgatory/purgatory.ro"
gnu_chk="$out/arch/riscv/purgatory/purgatory.chk"
if [[ ! -s "$gnu_chk" ]]; then
  echo "MINILD_PURGATORY_STATIC=INCONCLUSIVE missing_GNU_cache=$gnu_chk" >&2
  exit 8
fi
sha256sum "$gnu_chk" >>"$ev/input-sha256.txt"
start=$(date +%s%3N)
status=0
timeout 90s "$minild" -melf64lriscv -z noexecstack \
  --no-warn-rwx-segments -e purgatory_start -z nodefaultlib \
  "$gnu_ro" -o "$ev/purgatory.chk" \
  >"$ev/purgatory-static.stdout" 2>"$ev/purgatory-static.stderr" || status=$?
elapsed=$(($(date +%s%3N) - start))
printf '%s\t%s\n' "$elapsed" "$status" >"$ev/static-timing.tsv"
echo "MINILD_PURGATORY_STATIC_TIMING elapsed_ms=$elapsed rc=$status linux_C_compiles=0"
if (( status != 0 )); then
  tail -n 75 "$ev/purgatory-static.stderr" >&2
  exit "$status"
fi
python3 - "$gnu_chk" "$ev/purgatory.chk" <<'PYEXEC'
import struct,sys
from pathlib import Path
headers=[]
for p in map(Path,sys.argv[1:]):
    h=p.read_bytes()[:64]
    assert h[:6]==b'\x7fELF\x02\x01',f'ELF64LE: {p}'
    assert struct.unpack_from('<H',h,16)[0]==2,f'ET_EXEC: {p}'
    assert struct.unpack_from('<H',h,18)[0]==243,f'RISC-V: {p}'
    print(f'MINILD_PURGATORY_STATIC_ELF=PASS path={p} RV64_EXEC=true')
    headers.append(h)
assert headers[0][48:52]==headers[1][48:52],'GNU/MiniLD ABI mismatch'
assert all(struct.unpack_from('<Q',h,24)[0]!=0 for h in headers),'missing ELF entry'
print('MINILD_PURGATORY_STATIC_ABI=PASS same_GNU_flags=true entry_nonzero=true')
PYEXEC
riscv64-linux-gnu-nm --defined-only -g -P "$gnu_chk" >"$ev/static-gnu-nm.txt"
riscv64-linux-gnu-nm --defined-only -g -P "$ev/purgatory.chk" >"$ev/static-minild-nm.txt"
cut -d' ' -f1 "$ev/static-gnu-nm.txt" | LC_ALL=C sort -u >"$ev/static-gnu-names.txt"
cut -d' ' -f1 "$ev/static-minild-nm.txt" | LC_ALL=C sort -u >"$ev/static-minild-names.txt"
if ! cmp -s "$ev/static-gnu-names.txt" "$ev/static-minild-names.txt"; then
  diff -u "$ev/static-gnu-names.txt" "$ev/static-minild-names.txt" >"$ev/static-diff.txt" || true
  echo "MINILD_PURGATORY_STATIC=INCONCLUSIVE exported_symbol_mismatch" >&2
  head -n 50 "$ev/static-diff.txt" >&2
  exit 8
fi
sha256sum "$ev/purgatory.chk" >"$ev/static-output-sha256.txt"
echo "MINILD_PURGATORY_STATIC=PASS real_GNU_linux_purgatory_chk=true rv64_elf_and_symbols_match=true no_Linux_C_rebuild=true runtime_claim=false"

