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

# Collect every independent TU and phase failure before failing the job.
# In particular, a strict ELF mismatch MUST NOT suppress a real GNU link or
# QEMU attempt when both MiniAS objects exist.
failures=()
declare -A gnu_object=() mini_object=() runtime_ran=()
strict_equal=0
: >"$work/evidence/failures.txt"
fail_stage() {
  failures+=("$1")
  printf 'MINIAS_GCC_AB_STAGE=FAIL arch=%s reason=%s\n' "$arch" "$1" |
    tee -a "$work/evidence/failures.txt" >&2
}

# Both sides consume these same, SHA-recorded bytes from one GCC invocation.
# Real libc calls, globals and two-TU calls require external ELF relocations.
for unit in main helper; do
  source="$repo/tests/assembler/runtime-gcc-single-variable/$unit.c"
  rm -f "$work/asm/$unit.s" "$work/gnu/$unit.o" "$work/mini/$unit.o"
  if "$cc" "${gcc_target_flags[@]}" -O1 -g0 -fno-pie -fno-pic \
      -Wall -Wextra -Werror -S "$source" -o "$work/asm/$unit.s" \
      >"$work/evidence/gcc-$unit-s.log" 2>&1 && test -s "$work/asm/$unit.s"; then
    sha256sum "$source" "$work/asm/$unit.s" >>"$work/evidence/sources.sha256"
  else
    fail_stage "gcc-S:$unit"
    continue
  fi

  if "$gas" "${gas_flags[@]}" -o "$work/gnu/$unit.o" "$work/asm/$unit.s" \
      >"$work/evidence/gnu-$unit-as.log" 2>&1 && test -s "$work/gnu/$unit.o"; then
    gnu_object["$unit"]=1
  else
    fail_stage "gnu-as:$unit"
  fi
  if "$minias" "${mini_flags[@]}" -o "$work/mini/$unit.o" "$work/asm/$unit.s" \
      >"$work/evidence/mini-$unit-as.log" 2>&1 && test -s "$work/mini/$unit.o"; then
    mini_object["$unit"]=1
  else
    fail_stage "minias:$unit"
  fi

  for flavor in gnu mini; do
    if [[ "$flavor" == gnu && ${gnu_object[$unit]:-0} != 1 ]] ||
       [[ "$flavor" == mini && ${mini_object[$unit]:-0} != 1 ]]; then
      continue
    fi
    if ! "$readelf" -h "$work/$flavor/$unit.o" \
        >"$work/evidence/$flavor-$unit-header.txt" ||
       ! grep -Fq "$machine" "$work/evidence/$flavor-$unit-header.txt"; then
      fail_stage "elf-machine:$flavor:$unit"
    fi
    if ! "$readelf" -Wr "$work/$flavor/$unit.o" \
        >"$work/evidence/$flavor-$unit-relocs.txt"; then
      fail_stage "elf-relocations:$flavor:$unit"
    fi
  done

  if [[ ${gnu_object[$unit]:-0} == 1 && ${mini_object[$unit]:-0} == 1 ]]; then
    if python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
        --json-out "$work/evidence/$unit-elf-semantic.json" \
        "$work/gnu/$unit.o" "$work/mini/$unit.o" \
        >"$work/evidence/$unit-elf-semantic.log" 2>&1; then
      strict_equal=$((strict_equal + 1))
    else
      fail_stage "strict-elf:$unit"
    fi
  else
    printf 'MINIAS_GCC_AB_STAGE=SKIP arch=%s strict-elf=%s missing_object\n' \
      "$arch" "$unit"
  fi
done

# Use the GCC driver only to supply genuine CRT/glibc/libgcc. Force GNU
# BFD linker and a non-PIE static ELF; no custom linker or Mini runtime.
# Link and run independently of strict ELF equality; retain both results.
for flavor in gnu mini; do
  rm -f "$work/$flavor/program"
  if [[ "$flavor" == gnu &&
        ( ${gnu_object[main]:-0} != 1 || ${gnu_object[helper]:-0} != 1 ) ]] ||
     [[ "$flavor" == mini &&
        ( ${mini_object[main]:-0} != 1 || ${mini_object[helper]:-0} != 1 ) ]]; then
    printf 'MINIAS_GCC_AB_STAGE=SKIP arch=%s link=%s missing_object\n' "$arch" "$flavor"
    continue
  fi

  if ! "$cc" "${gcc_target_flags[@]}" -static -no-pie -fuse-ld=bfd \
      -Wl,--fatal-warnings -Wl,-v \
      -o "$work/$flavor/program" \
      "$work/$flavor/main.o" "$work/$flavor/helper.o" \
      >"$work/evidence/$flavor-link.log" 2>&1 ||
     ! test -s "$work/$flavor/program"; then
    fail_stage "gnu-bfd-link:$flavor"
    continue
  fi

  elf_ok=1
  if ! grep -Fq 'GNU ld' "$work/evidence/$flavor-link.log"; then
    fail_stage "linker-provenance:$flavor"
    elf_ok=0
  fi
  if ! "$readelf" -h "$work/$flavor/program" \
      >"$work/evidence/$flavor-executable-header.txt" ||
     ! grep -Fq "$machine" "$work/evidence/$flavor-executable-header.txt" ||
     ! grep -Eq 'Type:[[:space:]]+EXEC' \
       "$work/evidence/$flavor-executable-header.txt"; then
    fail_stage "link-elf-header:$flavor"
    elf_ok=0
  fi
  if ! "$readelf" -l "$work/$flavor/program" \
      >"$work/evidence/$flavor-program-headers.txt" ||
     grep -q INTERP "$work/evidence/$flavor-program-headers.txt"; then
    fail_stage "not-static-glibc:$flavor"
    elf_ok=0
  fi
  if (( elf_ok == 0 )); then
    printf 'MINIAS_GCC_AB_STAGE=SKIP arch=%s qemu=%s unsafe_executable\n' "$arch" "$flavor"
    continue
  fi

  if timeout --signal=TERM 30s "$qemu" "$work/$flavor/program" \
      >"$work/evidence/$flavor-stdout.txt" \
      2>"$work/evidence/$flavor-stderr.txt"; then
    result=0
  else
    result=$?
  fi
  runtime_ran["$flavor"]=1
  printf '%s\n' "$result" >"$work/evidence/$flavor-exit.txt"
  if (( result != 0 )); then
    fail_stage "qemu-exit:$flavor:$result"
  fi
done

if [[ ${runtime_ran[gnu]:-0} == 1 && ${runtime_ran[mini]:-0} == 1 ]]; then
  for channel in exit stdout stderr; do
    if ! cmp -s "$work/evidence/gnu-$channel.txt" \
                "$work/evidence/mini-$channel.txt"; then
      fail_stage "runtime-diff:$channel"
    fi
  done
  for flavor in gnu mini; do
    if ! grep -Fxq 'MINIAS_GCC_RUNTIME value=74 bias=11 length=10 status=PASS' \
        "$work/evidence/$flavor-stdout.txt"; then
      fail_stage "runtime-contract:$flavor"
    fi
  done
  if test -s "$work/evidence/gnu-stderr.txt" ||
     test -s "$work/evidence/mini-stderr.txt"; then
    fail_stage 'runtime-stderr-nonempty'
  fi
fi

if test -s "$work/gnu/program" && test -s "$work/mini/program"; then
  sha256sum "$work/gnu/program" "$work/mini/program" \
    >"$work/evidence/executables.sha256"
fi

if (( ${#failures[@]} != 0 )); then
  printf 'MINIAS_GCC_AB=FAIL arch=%s gcc_units=2 strict_elf_equal=%s/2 failures=%s\n' \
    "$arch" "$strict_equal" "${failures[*]}" | tee "$work/evidence/summary.txt" >&2
  exit 1
fi
echo "MINIAS_GCC_AB=PASS arch=$arch units=2 gcc_source_same=1 strict_elf=2 static_glibc=1 gnu_bfd=1 qemu=2 stdout_equal=1 exit_equal=1" |
  tee "$work/evidence/summary.txt"
