#!/usr/bin/env bash
# One experimental MiniAS object, all 2063 other MiniC C objects frozen.
# This does NOT modify the canonical 2064-object cache. Only the separate
# pool may be used by the existing QEMU/Kbuild trial with strict attestation.
set -Eeuo pipefail
if (( $# != 5 )); then
  echo "usage: $0 <artifact-dir> <certified-minic-pool> <experimental-pool> <minias-tool-dir> <evidence-dir>" >&2
  exit 64
fi
artifact=$(realpath "$1")
frozen=$(realpath "$2")
experiment=$(realpath -m "$3")
toolchain=$(realpath -m "$4")
ev=$(realpath -m "$5")
repo=$(cd "$(dirname "$0")/../.." && pwd)
src="$artifact/trials/full_all/earlycon-source/earlycon-minic.s"
relative=drivers/tty/serial/earlycon.o
test -s "$src"
test -s "$frozen/$relative"
mkdir -p "$ev" "$experiment"
printf '%s  %s\n' 5a2d6035541018dbacbcc8b7a838f24239d934fd3c3b7fdb3e414e23ddacc02b "$src" | sha256sum -c -
# Cross-check original manifest to ensure the experimental pool really starts
# from a fully authenticated 2064-member MiniC/GNU-AS accepted build.
manifest="$(dirname "$frozen")/minic-manifest.sha256"
test -s "$manifest"
test "$(wc -l < "$manifest")" -eq 2064
(cd "$(dirname "$frozen")" && sha256sum -c "$manifest" --quiet)
cp -a --reflink=auto "$frozen/." "$experiment/"
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" "$toolchain/bin/minic-as"
minias="$toolchain/bin/minic-as"
sha256sum "$minias" >"$ev/minias-tool.sha256"
sha256sum "$src" >"$ev/input-assembly.sha256"
sha256sum "$frozen/$relative" >"$ev/certified-earlycon-object.sha256"
start=$(date +%s%3N)
"$minias" -march=rv64gc -mabi=lp64d -o "$experiment/$relative" "$src" >"$ev/minias.stdout" 2>"$ev/minias.stderr"
elapsed=$(($(date +%s%3N)-start))
echo "MINIAS_EARLYCON_EXPERIMENT=ASSEMBLED elapsed_ms=$elapsed native_object=1 original_2064_cache_unchanged=true"
python3 - "$frozen/$relative" "$experiment/$relative" "$manifest" "$experiment" <<'PY_ATTEST'
import hashlib,struct,sys
from pathlib import Path
orig,variant,manifest,pool=map(Path,sys.argv[1:])
a,b=orig.read_bytes()[:52],variant.read_bytes()[:52]
for h in (a,b):
    assert len(h)==52 and h[:6]==b'\x7fELF\x02\x01', 'ELF64 LE required'
    assert struct.unpack_from('<H',h,16)[0]==1, 'ET_REL required'
    assert struct.unpack_from('<H',h,18)[0]==243, 'RISC-V required'
assert a[48:52]==b[48:52], 'MiniAS ABI flags differ from certified GNU-AS'
assert orig.read_bytes()!=variant.read_bytes(), 'candidate identical; invalid experiment'
skipped=0
checked=0
for line in manifest.read_text().splitlines():
    want,rel=line.split(maxsplit=1)
    assert rel.startswith('minic/'), rel
    rel=rel.removeprefix('minic/')
    target=pool/rel
    assert target.is_file(), rel
    if rel=='drivers/tty/serial/earlycon.o':
        skipped+=1
        continue
    actual=hashlib.sha256(target.read_bytes()).hexdigest()
    assert actual==want, f'candidate pool contamination:{rel}'
    checked+=1
assert skipped==1 and checked==2063, (skipped,checked)
print(f'MINIAS_EARLYCON_POOL=PASS frozen_minic_gnuas_members=2063 native_minias_members=1 copied_pool_SHA_attested=true rv64_abi={a[48:52].hex()}')
PY_ATTEST
for which in original experimental; do
  obj="$frozen/$relative"
  if [[ "$which" == experimental ]]; then obj="$experiment/$relative"; fi
  riscv64-linux-gnu-nm -g --defined-only -P "$obj" >"$ev/$which.exports.txt"
  cut -d' ' -f1 "$ev/$which.exports.txt" | LC_ALL=C sort -u >"$ev/$which.export-names.txt"
done
if ! cmp -s "$ev/original.export-names.txt" "$ev/experimental.export-names.txt"; then
  diff -u "$ev/original.export-names.txt" "$ev/experimental.export-names.txt" >"$ev/export-name-diff.txt" || true
  echo "MINIAS_EARLYCON_POOL=FAIL global_export_symbol_set_changed" >&2
  head -n 55 "$ev/export-name-diff.txt" >&2
  exit 8
fi
sha256sum "$experiment/$relative" >"$ev/experimental-earlycon-object.sha256"
echo "MINIAS_EARLYCON_POOL=READY exported_names_match=true 2063_frozen=true one_native=true full_QEMU_runtime_NOT_YET_PROVEN=true"
