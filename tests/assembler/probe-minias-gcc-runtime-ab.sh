#!/usr/bin/env bash
# One controlled substitution: GCC emits exactly two .s files; only the
# assembler differs. GCC/GNU binutils provide crt, static glibc and GNU ld.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-runtime-ab.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-gcc-runtime-ab/$arch"}
mkdir -p "$work/asm" "$work/gnu" "$work/mini" "$work/evidence"
: >"$work/evidence/sources.sha256"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}
    gas=${GNU_AS:-as}
    readelf=${GNU_READELF:-readelf}
    qemu=${QEMU_USER:-qemu-x86_64}
    gas_flags=(--64)
    gcc_target_flags=()
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    machine='Advanced Micro Devices X86-64'
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}
    gas=${GNU_AS:-aarch64-linux-gnu-as}
    readelf=${GNU_READELF:-aarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-aarch64}
    gas_flags=(-march=armv8-a)
    gcc_target_flags=()
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    machine='AArch64'
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}
    gas=${GNU_AS:-riscv64-linux-gnu-as}
    readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-riscv64}
    # GNU linker relaxation changes relocation contracts. Both sides are
    # deliberately pinned to the nonrelaxing GNU assembler input policy.
    gcc_target_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    gas_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    machine='RISC-V'
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gas=${GNU_AS:-loongarch64-linux-gnu-as}
    readelf=${GNU_READELF:-loongarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-loongarch64}
    # The installed LoongArch GCC13 defaults its ISA profile. lp64d is
    # explicit and must agree with the static cross-glibc ABI.
    gcc_target_flags=(-mabi=lp64d)
    gas_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    machine='LoongArch'
    ;;
  *) echo "MINIAS_GCC_AB=ERROR unsupported_target:$arch" >&2; exit 64 ;;
esac

for executable in "$cc" "$gas" "$readelf" "$qemu" "$minias" python3 timeout; do
  command -v "$executable" >/dev/null || {
    echo "MINIAS_GCC_AB=ERROR missing_tool:$executable" >&2
    exit 127
  }
done

"$cc" --version >"$work/evidence/gcc-version.txt"
"$gas" --version >"$work/evidence/gnu-as-version.txt"
"$readelf" --version >"$work/evidence/gnu-readelf-version.txt"
"$minias" --version >"$work/evidence/minias-version.txt" 2>&1 || true
"$cc" -print-prog-name=ld.bfd >"$work/evidence/gnu-linker-path.txt"
sha256sum "$minias" >"$work/evidence/minias-binary.sha256"
printf 'source_sha=%s\narch=%s\n' "${GITHUB_SHA:-local-uncommitted}" "$arch" \
  >"$work/evidence/run-identity.txt"

# Both sides consume these same, SHA-recorded bytes from one GCC invocation.
# Real libc calls, globals and two-TU calls require external ELF relocations.
for unit in main helper; do
  source="$repo/tests/assembler/runtime-gcc-single-variable/$unit.c"
  "$cc" "${gcc_target_flags[@]}" -O1 -g0 -fno-pie -fno-pic -Wall -Wextra -Werror \
    -S "$source" -o "$work/asm/$unit.s"
  sha256sum "$source" "$work/asm/$unit.s" >>"$work/evidence/sources.sha256"
  "$gas" "${gas_flags[@]}" -o "$work/gnu/$unit.o" "$work/asm/$unit.s" \
    >"$work/evidence/gnu-$unit-as.log" 2>&1
  "$minias" "${mini_flags[@]}" -o "$work/mini/$unit.o" "$work/asm/$unit.s" \
    >"$work/evidence/mini-$unit-as.log" 2>&1
  test -s "$work/gnu/$unit.o" && test -s "$work/mini/$unit.o"
  "$readelf" -h "$work/gnu/$unit.o" >"$work/evidence/gnu-$unit-header.txt"
  "$readelf" -h "$work/mini/$unit.o" >"$work/evidence/mini-$unit-header.txt"
  grep -Fq "$machine" "$work/evidence/gnu-$unit-header.txt"
  grep -Fq "$machine" "$work/evidence/mini-$unit-header.txt"
  "$readelf" -Wr "$work/gnu/$unit.o" >"$work/evidence/gnu-$unit-relocs.txt"
  "$readelf" -Wr "$work/mini/$unit.o" >"$work/evidence/mini-$unit-relocs.txt"
  python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
    --json-out "$work/evidence/$unit-elf-semantic.json" \
    "$work/gnu/$unit.o" "$work/mini/$unit.o" \
    >"$work/evidence/$unit-elf-semantic.log"
done

# Use the GCC driver only to supply genuine CRT/glibc/libgcc. Force GNU
# BFD linker and a non-PIE static ELF; no custom linker or Mini runtime.
for flavor in gnu mini; do
  "$cc" "${gcc_target_flags[@]}" -static -no-pie -fuse-ld=bfd \
    -Wl,--fatal-warnings -Wl,-v \
    -o "$work/$flavor/program" \
    "$work/$flavor/main.o" "$work/$flavor/helper.o" \
    >"$work/evidence/$flavor-link.log" 2>&1
  test -s "$work/$flavor/program"
  grep -Fq 'GNU ld' "$work/evidence/$flavor-link.log"
  "$readelf" -h "$work/$flavor/program" >"$work/evidence/$flavor-executable-header.txt"
  "$readelf" -l "$work/$flavor/program" >"$work/evidence/$flavor-program-headers.txt"
  grep -Fq "$machine" "$work/evidence/$flavor-executable-header.txt"
  grep -Eq 'Type:[[:space:]]+EXEC' "$work/evidence/$flavor-executable-header.txt"
  if grep -q INTERP "$work/evidence/$flavor-program-headers.txt"; then
    echo "MINIAS_GCC_AB=FAIL dynamic_link:$flavor" >&2
    exit 1
  fi
  set +e
  timeout --signal=TERM 30s "$qemu" "$work/$flavor/program" \
    >"$work/evidence/$flavor-stdout.txt" \
    2>"$work/evidence/$flavor-stderr.txt"
  result=$?
  set -e
  printf '%s\n' "$result" >"$work/evidence/$flavor-exit.txt"
  if (( result != 0 )); then
    echo "MINIAS_GCC_AB=FAIL runtime:$arch:$flavor:exit=$result" >&2
    exit 1
  fi
done

cmp "$work/evidence/gnu-exit.txt" "$work/evidence/mini-exit.txt"
cmp "$work/evidence/gnu-stdout.txt" "$work/evidence/mini-stdout.txt"
cmp "$work/evidence/gnu-stderr.txt" "$work/evidence/mini-stderr.txt"
grep -Fxq 'MINIAS_GCC_RUNTIME value=74 bias=11 length=10 status=PASS' \
  "$work/evidence/mini-stdout.txt"
test ! -s "$work/evidence/mini-stderr.txt"

sha256sum "$work/gnu/program" "$work/mini/program" \
  >"$work/evidence/executables.sha256"
echo "MINIAS_GCC_AB=PASS arch=$arch units=2 gcc_source_same=1 strict_elf=2 static_glibc=1 gnu_bfd=1 qemu=2 stdout_equal=1 exit_equal=1"
