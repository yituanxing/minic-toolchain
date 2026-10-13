#!/usr/bin/env bash
# Fast performance oracle: replay ONLY the first final MiniLD kernel link.
# Input objects are an existing SHA-pinned, QEMU-certified GNU Kbuild cache.
# This is *not* a replacement for the 2064-MiniC-object runtime proof.
set -Eeuo pipefail
if [[ $# != 3 ]]; then
  echo "usage: $0 <gnu-kbuild-out> <minild-build-dir> <evidence-dir>" >&2
  exit 64
fi
out=$(realpath "$1")
toolchain=$(realpath -m "$2")
ev=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
for input in vmlinux.a .vmlinux.export.o init/version-timestamp.o \
    drivers/firmware/efi/libstub/lib.a \
    arch/riscv/kernel/vmlinux.lds; do
  [[ -s "$out/$input" ]] || {
    echo "MINILD_CACHE_REPLAY=INCONCLUSIVE missing_input=$input" >&2
    exit 8
  }
  sha256sum "$out/$input" >>"$ev/input-sha256.txt"
done
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" \
  "$toolchain/bin/minic-ld"
minild="$toolchain/bin/minic-ld"
sha256sum "$minild" >"$ev/minild-tool-sha256.txt"
# Link from the fully frozen GNU object pool; do NOT call Kbuild, GNU -E,
# MiniC -S, assembler, archiver or QEMU. Repeating this command makes a
# direct A/B timing fixture for changes only in the MiniLD source.
(
  cd "$out"
  start=$(date +%s%3N)
  status=0
  MINILD_PROFILE_PHASES=1 timeout 180s "$minild" \
    -melf64lriscv -z noexecstack --no-warn-rwx-segments \
    -z norelro --build-id=sha1 --orphan-handling=warn \
    --script=./arch/riscv/kernel/vmlinux.lds --strip-debug \
    -o "$ev/.tmp_vmlinux.kallsyms1" \
    --whole-archive vmlinux.a .vmlinux.export.o init/version-timestamp.o \
    --no-whole-archive --start-group \
    ./drivers/firmware/efi/libstub/lib.a --end-group \
    >"$ev/ld.stdout" 2>"$ev/ld.stderr" || status=$?
  elapsed=$(($(date +%s%3N)-start))
  printf '%s\t%s\n' "$elapsed" "$status" >"$ev/timing.tsv"
  echo "MINILD_CACHE_REPLAY_TIMING elapsed_ms=$elapsed rc=$status gnu_c_pool=true minic_c_recompiled=0"
  if [[ $status != 0 ]]; then
    tail -n 80 "$ev/ld.stderr" >&2
    exit "$status"
  fi
)
elf="$ev/.tmp_vmlinux.kallsyms1"
[[ -s "$elf" ]] || { echo "MINILD_CACHE_REPLAY=FAIL missing_output" >&2; exit 9; }
riscv64-linux-gnu-readelf -h "$elf" >"$ev/elf-header.txt"
grep -Eq 'Machine:.*RISC-V' "$ev/elf-header.txt"
riscv64-linux-gnu-readelf -S "$elf" >"$ev/elf-sections.txt"
riscv64-linux-gnu-nm "$elf" >"$ev/elf-symbols.txt"
grep -Eq ' [Tt] _text$| [Tt] _start$| [Tt] start_kernel
  echo "MINILD_CACHE_REPLAY=FAIL expected_kernel_symbols_absent" >&2
  exit 10
}
sha256sum "$elf" >"$ev/output.sha256"
grep '^MINILD_PHASE=' "$ev/ld.stderr" || true
echo "MINILD_CACHE_REPLAY=PASS source=pinned_GNU_linux_6.6.143_only native_minild=true kbuild_invocations=0 linux_C_compiles=0 runtime_claim=false"
 "$ev/elf-symbols.txt" || {
  echo "MINILD_CACHE_REPLAY=FAIL expected_kernel_symbols_absent" >&2
  exit 10
}
sha256sum "$elf" >"$ev/output.sha256"
grep '^MINILD_PHASE=' "$ev/ld.stderr" || true
echo "MINILD_CACHE_REPLAY=PASS source=pinned_GNU_linux_6.6.143_only native_minild=true kbuild_invocations=0 linux_C_compiles=0 runtime_claim=false"
