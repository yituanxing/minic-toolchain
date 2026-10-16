#!/usr/bin/env bash
# Component-level refactor check: build ELF32 writer from its own .o via
# MINILD_SOURCES, never by including a second .c in tools/minic-ld/main.c.
# Reuse four frozen Linux compat-vDSO .o; do not build 2064 C owners here.
# Exact four-object Linux 6.6.143 RISC-V compat-vDSO ELF32 linker frontier.
# This is an isolated cached replay, NOT a full Linux/QEMU acceptance.
# No C compilation, Kbuild rebuild, GNU output substitution, or new CI job.
set -Eeuo pipefail
# Fail before restoring the large fixture if profiling prerequisites are absent.
[[ -x /usr/bin/time ]] || {
  echo 'MINILD_VDSO32=INCONCLUSIVE missing_GNU_time' >&2
  exit 8
}
if (( $# != 3 )); then
  echo "usage: $0 <frozen-Kbuild-out> <MiniLD-build-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
tools=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
: >"$ev/inputs.sha256"
printf 'producer\tstatus\telapsed_s\tuser_s\tsys_s\tmaxrss_kb\toutput_bytes\n' >"$ev/measurements.tsv"

rel=arch/riscv/kernel/compat_vdso
inputs=(
  "$rel/rt_sigreturn.o"
  "$rel/getcpu.o"
  "$rel/flush_icache.o"
  "$rel/note.o"
)
for f in "${inputs[@]}" "$rel/compat_vdso.lds"; do
  if [[ ! -s "$out/$f" ]]; then
    printf 'MINILD_VDSO32=INCONCLUSIVE missing_frozen_input=%s\n' "$f" >&2
    exit 8
  fi
  sha256sum "$out/$f" >>"$ev/inputs.sha256"
  # These immutable input objects are tiny (ELF32 <2 KiB each) and permit
  # an offline local C linker replay instead of another full CI roundtrip.
  if [[ "$f" == *.o ]]; then
    cp -- "$out/$f" "$ev/${f##*/}"
  fi
done
# Keep per-input ELF metadata to detect mistaken ABI/cache selection.
# Include linker inputs' actual relocations/symbol tables: these describe
# the genuine ELF32 semantic frontier rather than a guessed ABI subset.
for object in "${inputs[@]}"; do
  printf '\n===== %s =====\n' "$object" >>"$ev/input-elf-summary.txt"
  riscv64-linux-gnu-readelf -Wh -SW -Wr -Ws "$out/$object" \
    >>"$ev/input-elf-summary.txt"
done
cp "$out/$rel/compat_vdso.lds" "$ev/compat-vdso-actual-linker-script.txt"

make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$tools" "$tools/bin/minic-ld"
minild="$tools/bin/minic-ld"
sha256sum "$minild" >"$ev/minild.sha256"
{
  echo 'Linux 6.6.143 frozen exact four-object compat-vDSO ELF32 shared link'
  printf 'linker_sha='; sha256sum "$minild"
  cat "$ev/inputs.sha256"
  printf 'reference=riscv64-linux-gnu-ld\n'
} >"$ev/identity.txt"

args=(
  -melf32lriscv -z noexecstack --no-warn-rwx-segments
  -shared -S -soname=linux-compat_vdso.so.1
  --build-id=sha1 --hash-style=both --eh-frame-hdr
  -T "$rel/compat_vdso.lds"
  "${inputs[@]}"
)
# GNU only supplies the read-only reference. The native result is never
# repaired with the GNU ELF; failures remain failures.
measure_link() {
  local producer=$1 executable=$2 target=$3 status=0
  local measurement="$ev/${producer}.time"
  (
    cd "$out"
    /usr/bin/time -f '%e\t%U\t%S\t%M' -o "$measurement" \
      timeout 90s "$executable" "${args[@]}" -o "$target" \
      >"$ev/${producer}.stdout" 2>"$ev/${producer}.stderr"
  ) || status=$?
  local elapsed=NA user=NA sys=NA maxrss=NA size=0
  if [[ -s "$measurement" ]]; then
    # A failed /usr/bin/time command can prepend a diagnostic line.
    last_time_line=$(tail -n 1 "$measurement")
    read -r elapsed user sys maxrss <<<"$last_time_line" || true
  fi
  [[ -f "$target" ]] && size=$(stat -c %s "$target")
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$producer" "$status" "$elapsed" "$user" "$sys" "$maxrss" "$size" \
    >>"$ev/measurements.tsv"
  echo "MINILD_VDSO32_TIME producer=$producer rc=$status elapsed_s=$elapsed maxrss_kb=$maxrss output_bytes=$size"
  return "$status"
}
gnu="$ev/gnu-compat-vdso.so"
native="$ev/native-compat-vdso.so"
if ! measure_link gnu /usr/bin/riscv64-linux-gnu-ld "$gnu"; then
  echo 'MINILD_VDSO32=INCONCLUSIVE frozen_GNU_reference_failed' >&2
  tail -n 20 "$ev/gnu.stderr" >&2
  exit 9
fi
riscv64-linux-gnu-readelf -Wh -SW -lW -dW --version-info "$gnu" >"$ev/gnu-elf.txt"
riscv64-linux-gnu-readelf -Ws "$gnu" >"$ev/gnu-symbols.txt"
if ! grep -q 'Class:[[:space:]]*ELF32' "$ev/gnu-elf.txt" || \
   ! grep -q 'Type:[[:space:]]*DYN' "$ev/gnu-elf.txt"; then
  echo 'MINILD_VDSO32=INCONCLUSIVE GNU_reference_not_ELF32_ET_DYN' >&2
  exit 9
fi
echo 'MINILD_VDSO32_REFERENCE=PASS ELF32_ET_DYN=true inputs=4 real_linux_script=true no_C_rebuild=true'

native_rc=0
measure_link native "$minild" "$native" || native_rc=$?
if (( native_rc != 0 )); then
  grep -m1 '^minic-ld:' "$ev/native.stderr" >"$ev/first-blocker.txt" || true
  echo "MINILD_VDSO32=GAP native_rc=$native_rc first_blocker=$(cat "$ev/first-blocker.txt")"
  tail -n 25 "$ev/native.stderr" >&2
  exit "$native_rc"
fi
riscv64-linux-gnu-readelf -Wh -SW -lW -dW --version-info "$native" >"$ev/native-elf.txt"
riscv64-linux-gnu-readelf -Ws "$native" >"$ev/native-symbols.txt"
if ! grep -q 'Class:[[:space:]]*ELF32' "$ev/native-elf.txt" || \
   ! grep -q 'Type:[[:space:]]*DYN' "$ev/native-elf.txt"; then
  echo 'MINILD_VDSO32=FAIL native_output_not_ELF32_ET_DYN' >&2
  exit 20
fi
# Check actual ABI/loader-visible contracts, not raw byte equality (different
# linkers may choose different valid layout offsets or build IDs).
for symbol in __vdso_rt_sigreturn __vdso_getcpu __vdso_flush_icache; do
  grep -q "$symbol" "$ev/native-symbols.txt" || {
    echo "MINILD_VDSO32=FAIL missing_symbol=$symbol" >&2; exit 21;
  }
done
for section in '.dynsym' '.dynstr' '.dynamic' '.hash' '.gnu.hash' '.gnu.version' '.gnu.version_d' '.eh_frame' '.eh_frame_hdr'; do
  grep -Fq "$section" "$ev/native-elf.txt" || {
    echo "MINILD_VDSO32=FAIL missing_section=$section" >&2; exit 22;
  }
done
for symbol in __vdso_rt_sigreturn __vdso_getcpu __vdso_flush_icache; do
  grep -Eq "${symbol}@@LINUX_4[.]15" "$ev/native-symbols.txt" || {
    echo "MINILD_VDSO32=FAIL missing_versioned_export=$symbol" >&2
    exit 27
  }
done
grep -q 'LINUX_4.15' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_LINUX_4.15_version' >&2; exit 23;
}
grep -q 'GNU_EH_FRAME' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_unwind_header_segment' >&2; exit 24;
}
grep -q 'DYNAMIC' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_dynamic_segment' >&2; exit 25;
}
grep -q 'NOTE' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_linux_note_segment' >&2; exit 26;
}
grep -q 'GNU_HASH' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_GNU_HASH_dynamic_tag' >&2; exit 28;
}
grep -q 'SONAME.*linux-compat_vdso.so.1' "$ev/native-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL bad_vdso_soname' >&2; exit 29;
}
riscv64-linux-gnu-readelf -nW "$native" >"$ev/native-notes.txt"
grep -q 'Build ID:' "$ev/native-notes.txt" || {
  echo 'MINILD_VDSO32=FAIL missing_native_build_id' >&2; exit 30;
}
# Compare executable RISC-V instructions from the exact same objects,
# rather than equating different but ABI-valid ELF metadata layouts.
python3 - "$gnu" "$native" <<'PY_ELF32_TEXT'
import struct
import sys
from pathlib import Path
def text_bytes(path):
    b = Path(path).read_bytes()
    assert b[:6] == b'\x7fELF\x01\x01'
    shoff = struct.unpack_from('<I', b, 32)[0]
    shentsize, nsec, shnames = struct.unpack_from('<HHH', b, 46)
    assert shentsize == 40 and nsec > 0 and shnames < nsec
    str_header = shoff + shnames*shentsize
    stroff, strsize = struct.unpack_from('<II', b, str_header+16)
    strings = b[stroff:stroff+strsize]
    for i in range(nsec):
        hdr = shoff+i*shentsize
        name, = struct.unpack_from('<I', b, hdr)
        end = strings.find(b'\x00',name)
        if end>=0 and strings[name:end] == b'.text':
            off, size = struct.unpack_from('<II', b, hdr+16)
            assert off+size <= len(b)
            return b[off:off+size]
    raise AssertionError('ELF32 missing .text')
assert text_bytes(sys.argv[1]) == text_bytes(sys.argv[2]), \
       'native ELF32 executable .text differs from GNU on same four objects'
print('MINILD_VDSO32_TEXT=PASS GNU_same_inputs_text_bytes_equal=true',
      'gnu_fallback=false')
PY_ELF32_TEXT
echo 'MINILD_VDSO32=PASS exact_Linux_ELF32_link=true native_output=true no_GNU_fallback=true cached_C=true'
# Real Kbuild overlays all 34 .S via MiniAS. Four are RV32 compat-vDSO.
# Do NOT equate GNU-assembled four .o above with actual MiniAS inputs.
# Replay four MiniAS objects from their EXACT frozen Kbuild .o.cmd, using
# GCC -E -P only as the independently established .S text oracle.
# This is seconds-scale source diagnosis, not 2064-C compilation.
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$tools" \
  "$tools/bin/minic-as"
python3 "$repo/tools/ci/minild-compat-vdso32-native-asm-replay-v1.py" \
  --out "$out" --minias "$tools/bin/minic-as" \
  --minild "$minild" --evidence "$ev" \
  >"$ev/native-S-replay.txt" 2>"$ev/native-S-replay.stderr" || {
    echo 'MINILD_VDSO32_NATIVE_S=FAIL first_real_MiniAS_object_gap=true no_C_rebuild=true' >&2
    cat "$ev/native-S-replay.stderr" >&2
    tail -n 60 "$ev/native-S-replay.txt" >&2
    exit 31
  }
cat "$ev/native-S-replay.txt"

echo 'MINILD_VDSO32=NEEDS_FULL_RUNTIME_CERTIFICATION QEMU_P1_not_run_in_this_gate=true'
