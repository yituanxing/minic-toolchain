#!/usr/bin/env bash
# Exact four-object Linux 6.6.143 RISC-V compat-vDSO ELF32 linker frontier.
# This is an isolated cached replay, NOT a full Linux/QEMU acceptance.
# No C compilation, Kbuild rebuild, GNU output substitution, or new CI job.
set -Eeuo pipefail
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
done

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
    IFS=$'\t' read -r elapsed user sys maxrss <"$measurement" || true
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
for symbol in __compat_vdso_rt_sigreturn __compat_vdso_getcpu __compat_vdso_flush_icache; do
  grep -q "$symbol" "$ev/native-symbols.txt" || {
    echo "MINILD_VDSO32=FAIL missing_symbol=$symbol" >&2; exit 21;
  }
done
for section in '.dynsym' '.dynstr' '.dynamic' '.hash' '.gnu.hash'; do
  grep -Fq "$section" "$ev/native-elf.txt" || {
    echo "MINILD_VDSO32=FAIL missing_section=$section" >&2; exit 22;
  }
done
echo 'MINILD_VDSO32=PASS exact_Linux_ELF32_link=true native_output=true no_GNU_fallback=true cached_C=true'
echo 'MINILD_VDSO32=NEEDS_FULL_RUNTIME_CERTIFICATION QEMU_P1_not_run_in_this_gate=true'
