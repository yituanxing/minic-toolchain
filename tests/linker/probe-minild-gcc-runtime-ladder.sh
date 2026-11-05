#!/usr/bin/env bash
# Real, staged static-link acceptance for MiniLD.  Reuse the MiniAS GCC C
# workload and its actual assembler as producer, then link those SAME objects
# with GNU BFD and MiniLD.  Both executable outputs must run and agree.
# This is deliberately separate from the MiniAS ladder, whose final linker is
# GNU BFD on both sides.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minild-gcc-runtime-ladder.sh x86_64|riscv64|riscv32|aarch64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=${WORK_DIR:-"$repo/build/minild-gcc-runtime-ladder/$arch"}
mkdir -p "$work/evidence"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; gnu_ld=${GNU_LD:-ld.bfd}
    readelf=${GNU_READELF:-readelf}; qemu=${QEMU_USER:-qemu-x86_64}
    gcc_flags=(); mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    emulation=elf_x86_64; machine='Advanced Micro Devices X86-64'
    isa=x86-64; abi=sysv
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}
    gnu_ld=${GNU_LD:-riscv64-linux-gnu-ld.bfd}
    readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-riscv64}
    case ${MINILD_RV64_PROFILE:-gcc-glibc} in
      gcc-glibc)
        isa=rv64gc; abi=lp64d ;;
      rv64i-lp64)
        isa=rv64i; abi=lp64 ;;
      *) echo 'MINILD_RUNTIME_LADDER=ERROR invalid_rv64_profile' >&2; exit 64 ;;
    esac
    gcc_flags=("-march=$isa" "-mabi=$abi" -mno-relax)
    mini_flags=(--target=riscv "-march=$isa" "-mabi=$abi")
    emulation=elf64lriscv; machine='RISC-V'
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}
    gnu_ld=${GNU_LD:-aarch64-linux-gnu-ld.bfd}
    readelf=${GNU_READELF:-aarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-aarch64}
    gcc_flags=(); mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    emulation=aarch64linux; machine='AArch64'; isa=armv8-a; abi=lp64
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gnu_ld=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
    readelf=${GNU_READELF:-loongarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-loongarch64}
    gcc_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    emulation=elf64loongarch; machine='LoongArch'; isa=loongarch64; abi=lp64d
    ;;
  riscv32)
    # GNU's riscv64-linux-gnu glibc has no RV32I/ILP32 runtime.  Dispatch to
    # the project's authentic RV32 Picolibc + Linux syscall bridge ladder;
    # the same GNU-BFD-vs-MiniLD runtime parity is mandatory there.
    exec bash "$repo/tests/linker/probe-rv32-picolibc-ladder.sh"
    ;;
  *) printf 'MINILD_RUNTIME_LADDER=ERROR unsupported_arch=%s\n' "$arch" >&2; exit 64 ;;
esac

echo "MINILD_RUNTIME_LADDER_CONFIG arch=$arch isa=$isa abi=$abi emulation=$emulation cc=$cc gnu_ld=$gnu_ld runner=$qemu"
for tool in "$cc" "$gnu_ld" "$readelf" "$minias" "$minild" timeout cmp sha256sum; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'MINILD_RUNTIME_LADDER=ERROR arch=%s missing_tool=%s\n' "$arch" "$tool" |
      tee "$work/evidence/summary.txt" >&2
    exit 127
  fi
done
if ! command -v "$qemu" >/dev/null 2>&1; then
  if [[ $arch == x86_64 && ${ALLOW_NATIVE_X86_64:-0} == 1 ]]; then
    qemu=() # Explicit opt-in only; CI normally uses QEMU even for x86-64.
    echo 'MINILD_RUNTIME_LADDER_RUNNER=native-x86_64'
  else
    printf 'MINILD_RUNTIME_LADDER=ERROR arch=%s missing_runner=%s\n' "$arch" "$qemu" |
      tee "$work/evidence/summary.txt" >&2
    exit 127
  fi
else
  qemu=("$qemu")
fi
if ! "$gnu_ld" --version | grep -Fq 'GNU ld'; then
  echo 'MINILD_RUNTIME_LADDER=ERROR gnu_reference_is_not_bfd' >&2
  exit 2
fi

"$cc" --version >"$work/evidence/gcc-version.txt"
"$gnu_ld" --version >"$work/evidence/gnu-ld-version.txt"
"$minias" --version >"$work/evidence/minias-version.txt" 2>&1 || true
"$minild" --version >"$work/evidence/minild-version.txt" 2>&1 || true
sha256sum "$minias" "$minild" >"$work/evidence/tools.sha256"
printf 'sha=%s\narch=%s\nabi=%s\nemulation=%s\n' \
  "${GITHUB_SHA:-local-uncommitted}" "$arch" "$abi" "$emulation" \
  >"$work/evidence/identity.txt"

# Do not ask GCC's collect2 to call MiniLD.  It supplies many GNU-ld driver
# options that MiniLD intentionally rejects; resolving the exact GCC CRT and
# static runtime inputs here keeps both final linkers on identical operands.
gcc_input() {
  local name=$1 resolved
  resolved=$("$cc" "${gcc_flags[@]}" "-print-file-name=$name") || return 1
  if [[ $resolved == "$name" || ! -f $resolved ]]; then
    printf 'MINILD_RUNTIME_LADDER=ERROR missing_gcc_runtime=%s resolved=%s\n' \
      "$name" "$resolved" >&2
    return 1
  fi
  printf '%s\n' "$resolved"
}
runtime_inputs=(crt1.o crti.o crtbeginT.o libc.a libgcc.a libgcc_eh.a crtend.o crtn.o)
: >"$work/evidence/runtime-paths.tsv"
: >"$work/evidence/runtime-paths.tsv"
for name in "${runtime_inputs[@]}"; do
  if ! resolved=$(gcc_input "$name"); then
    echo "MINILD_RUNTIME_LADDER=ERROR arch=$arch missing_static_runtime=$name" |
      tee "$work/evidence/summary.txt" >&2
    exit 127
  fi
  case "$name" in
    crt1.o) crt1=$resolved ;;
    crti.o) crti=$resolved ;;
    crtbeginT.o) crtbeginT=$resolved ;;
    libc.a) libc=$resolved ;;
    libgcc.a) libgcc=$resolved ;;
    libgcc_eh.a) libgcc_eh=$resolved ;;
    crtend.o) crtend=$resolved ;;
    crtn.o) crtn=$resolved ;;
  esac
  printf '%s\t%s\n' "$name" "$resolved" >>"$work/evidence/runtime-paths.tsv"
done

failures=(); pass_count=0
fail() {
  failures+=("$stage:$1")
  stage_failures+=("$1")
  printf 'MINILD_RUNTIME_LADDER_STAGE=FAIL arch=%s stage=%s reason=%s\n' \
    "$arch" "$stage" "$1" >&2
}
diagnostic() {
  local step=$1 log=$2
  if [[ -s $log ]]; then
    printf 'MINILD_RUNTIME_LADDER_DIAGNOSTIC arch=%s stage=%s step=%s file=%s\n' \
      "$arch" "$stage" "$step" "$log" >&2
    sed -n '1,28p' "$log" >&2
  fi
}

for stage in exit control memory multitu io; do
  stage_dir="$work/$stage"
  mkdir -p "$stage_dir/asm" "$stage_dir/mini" "$stage_dir/gnu" "$stage_dir/evidence"
  stage_failures=()
  case "$stage" in
    exit)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/exit.c")
      opt=(-O0); expected='' ;;
    control)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/control.c")
      opt=(-O1); expected='LADDER_CONTROL value=165 status=PASS' ;;
    memory)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/memory.c")
      opt=(-O2); expected='LADDER_MEMORY sum=135 status=PASS' ;;
    multitu)
      sources=("$repo/tests/assembler/runtime-gcc-single-variable/main.c"
               "$repo/tests/assembler/runtime-gcc-single-variable/helper.c")
      opt=(-O1); expected='MINIAS_GCC_RUNTIME value=74 bias=11 length=10 status=PASS' ;;
    io)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/io.c")
      opt=(-O2); expected='LADDER_IO sum=1015 status=PASS' ;;
  esac
  printf '%s\n' "$expected" >"$stage_dir/evidence/expected.stdout"
  if [[ -z $expected ]]; then : >"$stage_dir/evidence/expected.stdout"; fi
  : >"$stage_dir/evidence/inputs.sha256"
  : >"$stage_dir/evidence/inputs.sha256"
  objects=()
  for source in "${sources[@]}"; do
    unit=$(basename "$source" .c)
    asm="$stage_dir/asm/$unit.s"
    obj="$stage_dir/mini/$unit.o"
    rm -f "$asm" "$obj"
    if ! "$cc" "${gcc_flags[@]}" "${opt[@]}" -std=c11 -g0 \
         -fno-pie -fno-pic -Wall -Wextra -Werror -S "$source" -o "$asm" \
         >"$stage_dir/evidence/gcc-$unit.log" 2>&1 || [[ ! -s $asm ]]; then
      fail "gcc-S:$unit"
      diagnostic "gcc-S:$unit" "$stage_dir/evidence/gcc-$unit.log"
      continue
    fi
    if ! "$minias" "${mini_flags[@]}" -o "$obj" "$asm" \
         >"$stage_dir/evidence/minias-$unit.log" 2>&1 || [[ ! -s $obj ]]; then
      fail "minias:$unit"
      diagnostic "minias:$unit" "$stage_dir/evidence/minias-$unit.log"
      continue
    fi
    objects+=("$obj")
    sha256sum "$source" "$asm" "$obj" >>"$stage_dir/evidence/inputs.sha256"
  done
  if (( ${#objects[@]} != ${#sources[@]} )); then
    fail 'missing-object'
    continue
  fi

  # Both linkers get the identical MiniAS-produced .o inputs and GNU CRT,
  # archives and order.  Do not silently substitute a GNU-linked executable.
  link_inputs=("$crt1" "$crti" "$crtbeginT" "${objects[@]}"
               --start-group "$libgcc" "$libgcc_eh" "$libc" --end-group
               "$crtend" "$crtn")
  for flavor in gnu mini; do
    program="$stage_dir/$flavor/program"
    rm -f "$program" "$stage_dir/evidence/$flavor-exit.txt" \
      "$stage_dir/evidence/$flavor-stdout.txt" \
      "$stage_dir/evidence/$flavor-stderr.txt"
    if [[ $flavor == gnu ]]; then linker=$gnu_ld; else linker=$minild; fi
    if ! "$linker" -m "$emulation" -static -e _start \
         -o "$program" "${link_inputs[@]}" \
         >"$stage_dir/evidence/$flavor-link.log" 2>&1 || [[ ! -s $program ]]; then
      fail "link:$flavor"
      diagnostic "link:$flavor" "$stage_dir/evidence/$flavor-link.log"
      continue
    fi
    if ! "$readelf" -h "$program" >"$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! grep -Fq "$machine" "$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! grep -Eq 'Type:[[:space:]]+EXEC' "$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! "$readelf" -l "$program" >"$stage_dir/evidence/$flavor-program-headers.txt" ||
       grep -Fq INTERP "$stage_dir/evidence/$flavor-program-headers.txt"; then
      fail "invalid-static-elf:$flavor"
      continue
    fi
    if [[ $arch == aarch64 ]]; then
      # Preserve the actual BFD/MiniLD static-glibc section, relocation and
      # symbol tables for an exact fault-address-to-section differential.
      "$readelf" -SW "$program" >"$stage_dir/evidence/$flavor-sections.txt"
      "$readelf" -Wr "$program" >"$stage_dir/evidence/$flavor-relocations.txt"
      "$readelf" -Ws "$program" >"$stage_dir/evidence/$flavor-symbols.txt"
    fi
    if [[ $arch == x86_64 ]]; then
      # Static glibc genuinely installs variant-II TLS and resolves IFUNCs
      # through IRELATIVE before calling their selected implementations.
      # A file that links but drops either ABI contract is not a pass.
      if ! grep -Eq '^[[:space:]]+TLS[[:space:]]' \
           "$stage_dir/evidence/$flavor-program-headers.txt" ||
         ! "$readelf" -Wr "$program" > \
           "$stage_dir/evidence/$flavor-relocations.txt" ||
         ! grep -Fq R_X86_64_IRELATIVE \
           "$stage_dir/evidence/$flavor-relocations.txt" ||
         ! "$readelf" -Ws "$program" > \
           "$stage_dir/evidence/$flavor-symbols.txt" ||
         ! awk '$NF == "__rela_iplt_start" && $7 != "UND" &&
                 $2 !~ /^0+$/ { found=1 } END { exit !found }' \
           "$stage_dir/evidence/$flavor-symbols.txt" ||
         ! awk '$NF == "__rela_iplt_end" && $7 != "UND" &&
                 $2 !~ /^0+$/ { found=1 } END { exit !found }' \
           "$stage_dir/evidence/$flavor-symbols.txt"; then
        fail "missing-static-tls-or-ifunc:$flavor"
        continue
      fi
    fi
    result=0
    timeout --signal=TERM 30s "${qemu[@]}" "$program" \
      >"$stage_dir/evidence/$flavor-stdout.txt" \
      2>"$stage_dir/evidence/$flavor-stderr.txt" || result=$?
    printf '%s\n' "$result" >"$stage_dir/evidence/$flavor-exit.txt"
    if [[ $arch == aarch64 && $stage == exit && $flavor == mini &&
          $result == 139 ]]; then
      # One representative failure, not all five: QEMU -strace provides the
      # actual guest SIGSEGV si_addr; in_asm,cpu records the last guest PC.
      # Keep full logs in the uploaded artifact but print only bounded tails
      # in Actions output. Diagnostics must never change the acceptance.
      trace_log="$stage_dir/evidence/mini-qemu-strace.txt"
      cpu_log="$stage_dir/evidence/mini-qemu-last-pc.txt"
      trace_status=0
      timeout --signal=TERM 15s "${qemu[@]}" -strace \
        -d in_asm,cpu -D "$cpu_log" "$program" \
        >"$stage_dir/evidence/mini-qemu-trace-stdout.txt" \
        2>"$trace_log" || trace_status=$?
      printf '%s\n' "$trace_status" >"$stage_dir/evidence/mini-qemu-trace-exit.txt"
      echo "MINILD_AARCH64_GLIBC_FAULT_TRACE rerun_exit=$trace_status"
      if [[ -s $trace_log ]]; then
        echo 'MINILD_AARCH64_GLIBC_FAULT_TRACE strace_tail_begin'
        tail -n 24 "$trace_log"
      fi
      if [[ -s $cpu_log ]]; then
        echo 'MINILD_AARCH64_GLIBC_FAULT_TRACE guest_pc_tail_begin'
        tail -n 48 "$cpu_log"
      fi
    fi
    if (( result != 0 )); then fail "runtime-exit:$flavor:$result"; fi
    if ! cmp -s "$stage_dir/evidence/expected.stdout" \
                 "$stage_dir/evidence/$flavor-stdout.txt"; then
      fail "runtime-contract:$flavor"
    fi
    if [[ -s $stage_dir/evidence/$flavor-stderr.txt ]]; then
      fail "runtime-stderr:$flavor"
    fi
  done
  if [[ -f $stage_dir/evidence/gnu-exit.txt &&
        -f $stage_dir/evidence/mini-exit.txt ]]; then
    for channel in exit stdout stderr; do
      if ! cmp -s "$stage_dir/evidence/gnu-$channel.txt" \
                   "$stage_dir/evidence/mini-$channel.txt"; then
        fail "runtime-difference:$channel"
      fi
    done
  else
    fail 'missing-linked-runtime-side'
  fi
  if (( ${#stage_failures[@]} == 0 )); then
    ((pass_count += 1))
    echo "MINILD_RUNTIME_LADDER_STAGE=PASS arch=$arch stage=$stage mini_link=1 gnu_bfd=1 run=2 parity=1"
  fi
done

printf '%s\n' "${failures[@]}" >"$work/evidence/failures.txt"
if (( ${#failures[@]} != 0 )); then
  echo "MINILD_RUNTIME_LADDER=FAIL arch=$arch passed=$pass_count/5 reasons=${failures[*]}" |
    tee "$work/evidence/summary.txt" >&2
  exit 1
fi
echo "MINILD_RUNTIME_LADDER=PASS arch=$arch passed=5/5 minild_link=5 runtime_parity=5" |
  tee "$work/evidence/summary.txt"
