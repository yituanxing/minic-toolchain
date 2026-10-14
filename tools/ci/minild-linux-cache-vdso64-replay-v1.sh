#!/usr/bin/env bash
# Probe GNU LD's *real* RISC-V 64-bit vDSO shared-link boundary using a frozen
# Linux 6.6.143 Kbuild object cache. No Linux C build, no Kbuild and no QEMU.
# Preserve exact GNU arguments: do NOT silently ignore ELF metadata flags.
# Live gap gate: observed remaining GNU LD vDSO64 from #38485397284;
# only a new 64-bit shared-link implementation can remove this fallback.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <gnu-kbuild-out> <minild-build-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
tools=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
declare -a inputs=(
  arch/riscv/kernel/vdso/rt_sigreturn.o
  arch/riscv/kernel/vdso/vgettimeofday.o
  arch/riscv/kernel/vdso/getcpu.o
  arch/riscv/kernel/vdso/flush_icache.o
  arch/riscv/kernel/vdso/hwprobe.o
  arch/riscv/kernel/vdso/sys_hwprobe.o
  arch/riscv/kernel/vdso/note.o
)
for f in "${inputs[@]}" arch/riscv/kernel/vdso/vdso.lds; do
  [[ -s "$out/$f" ]] || {
    echo "MINILD_VDSO=INCONCLUSIVE missing_pinned_GNU_object=$f" >&2
    exit 8
  }
  sha256sum "$out/$f" >>"$ev/inputs.sha256"
done
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$tools" \
  "$tools/bin/minic-ld"
minild="$tools/bin/minic-ld"
sha256sum "$minild" >"$ev/tool.sha256"
(
  cd "$out"
  # GNU Kbuild deletes vdso.so.dbg.tmp after producing vdso.so.dbg.
  # Regenerate the EXACT reference link from the same frozen .o/.lds inputs,
  # rather than claiming the transient intermediate must remain in cache.
  riscv64-linux-gnu-ld -melf64lriscv -z noexecstack \
    --no-warn-rwx-segments -shared -S \
    -soname=linux-vdso.so.1 --build-id=sha1 \
    --hash-style=both --eh-frame-hdr \
    -T arch/riscv/kernel/vdso/vdso.lds \
    "${inputs[@]}" -o "$ev/gnu-vdso.so.dbg.tmp" \
    >"$ev/gnu-ld.stdout" 2>"$ev/gnu-ld.stderr"
  sha256sum "$ev/gnu-vdso.so.dbg.tmp" >"$ev/gnu-output.sha256"
  echo "MINILD_VDSO_REFERENCE=PASS source=same_frozen_Kbuild_inputs compiler_C=skipped"
  start=$(date +%s%3N)
  code=0
  timeout 90s "$minild" -melf64lriscv -z noexecstack \
    --no-warn-rwx-segments -shared -S \
    -soname=linux-vdso.so.1 --build-id=sha1 \
    --hash-style=both --eh-frame-hdr \
    -T arch/riscv/kernel/vdso/vdso.lds \
    "${inputs[@]}" -o "$ev/vdso.so.dbg.tmp" \
    >"$ev/ld.stdout" 2>"$ev/ld.stderr" || code=$?
  elapsed=$(($(date +%s%3N) - start))
  printf '%s\t%s\n' "$elapsed" "$code" >"$ev/timing.tsv"
  echo "MINILD_VDSO_LINK elapsed_ms=$elapsed rc=$code no_Linux_C_recompile=true"
  if (( code != 0 )); then
    echo "MINILD_VDSO=GAP exact_GNU_shared_link_not_yet_supported" >&2
    tail -n 55 "$ev/ld.stderr" >&2
    exit "$code"
  fi
)
ref="$ev/gnu-vdso.so.dbg.tmp"
test -s "$ev/vdso.so.dbg.tmp"
for a in "$ref" "$ev/vdso.so.dbg.tmp"; do
  riscv64-linux-gnu-readelf -Wh "$a" >"$ev/$(basename "$a").header.$(sha256sum "$a" | cut -c1-8).txt"
done
riscv64-linux-gnu-readelf -W -lS -d "$ref" >"$ev/gnu-vdso.txt"
riscv64-linux-gnu-readelf -W -lS -d "$ev/vdso.so.dbg.tmp" >"$ev/native-vdso.txt"
python3 - "$ref" "$ev/vdso.so.dbg.tmp" <<'PY'
import struct,sys
from pathlib import Path
for p in map(Path,sys.argv[1:]):
  b=p.read_bytes()
  if b[:6]!=b'\x7fELF\x02\x01':
    raise SystemExit(f'MINILD_VDSO=FAIL not_ELF64LE: {p}')
  if struct.unpack_from('<H',b,16)[0]!=3:
    raise SystemExit(f'MINILD_VDSO=FAIL not_ET_DYN: {p}')
  if struct.unpack_from('<H',b,18)[0]!=243:
    raise SystemExit(f'MINILD_VDSO=FAIL not_RISCV: {p}')
  print(f'MINILD_VDSO_ELF=PASS {p}')
a,b=[Path(p).read_bytes() for p in sys.argv[1:]]
if a[48:52]!=b[48:52]:
  raise SystemExit('MINILD_VDSO=FAIL ABI_flags_differ')
print('MINILD_VDSO_ABI=PASS')
PY
for section in .hash .gnu.hash .eh_frame_hdr .dynamic .dynsym; do
  grep -Fq "$section " "$ev/gnu-vdso.txt" || {
    echo "MINILD_VDSO=INCONCLUSIVE GNU_missing_required_section:$section" >&2
    exit 8
  }
  grep -Fq "$section " "$ev/native-vdso.txt" || {
    echo "MINILD_VDSO=FAIL native_missing_section:$section" >&2
    exit 10
  }
done
grep -Fq 'linux-vdso.so.1' "$ev/native-vdso.txt" || {
  echo "MINILD_VDSO=FAIL missing_SONAME" >&2
  exit 11
}
sha256sum "$ev/vdso.so.dbg.tmp" >"$ev/output.sha256"
echo "MINILD_VDSO=PASS exact_GNU_flags=true ELF64_RISCV_ET_DYN=true hash_both=true eh_frame_hdr=true SONAME=true no_runtime_claim=true"
