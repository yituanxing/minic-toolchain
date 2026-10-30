#!/usr/bin/env bash
# Progressive *execution* acceptance for MiniAS. GCC generates one .s per TU;
# GNU and MiniAS assemble those SAME bytes; GNU BFD/CRT/libgcc/static glibc
# link both results. No object-byte or strict-ELF equality is a pass gate.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-runtime-ladder.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-gcc-runtime-ladder/$arch"}
mkdir -p "$work/evidence"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; gas=${GNU_AS:-as}; readelf=${GNU_READELF:-readelf}
    qemu=${QEMU_USER:-qemu-x86_64}
    gas_flags=(--64); gcc_flags=()
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    machine='Advanced Micro Devices X86-64'
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}
    gas=${GNU_AS:-aarch64-linux-gnu-as}
    readelf=${GNU_READELF:-aarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-aarch64}
    gas_flags=(-march=armv8-a); gcc_flags=()
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    machine='AArch64'
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}
    gas=${GNU_AS:-riscv64-linux-gnu-as}
    readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-riscv64}
    gcc_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    gas_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    machine='RISC-V'
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gas=${GNU_AS:-loongarch64-linux-gnu-as}
    readelf=${GNU_READELF:-loongarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-loongarch64}
    gcc_flags=(-mabi=lp64d); gas_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    machine='LoongArch'
    ;;
  *) echo "MINIAS_RUNTIME_LADDER=ERROR unsupported_arch=$arch" >&2; exit 64 ;;
esac

for tool in "$cc" "$gas" "$readelf" "$qemu" "$minias" timeout sha256sum cmp; do
  if ! command -v "$tool" >/dev/null; then
    echo "MINIAS_RUNTIME_LADDER=ERROR arch=$arch missing_tool=$tool" >&2
    exit 127
  fi
done

"$cc" --version >"$work/evidence/gcc-version.txt"
"$gas" --version >"$work/evidence/gnu-as-version.txt"
"$minias" --version >"$work/evidence/minias-version.txt" 2>&1 || true
"$cc" -print-prog-name=ld.bfd >"$work/evidence/gnu-ld-path.txt"
sha256sum "$minias" >"$work/evidence/minias.sha256"
printf 'sha=%s\narch=%s\nrunner=%s\n' "${GITHUB_SHA:-local-uncommitted}" \
  "$arch" "$qemu" >"$work/evidence/identity.txt"

failures=()
pass_count=0
stages=(exit control memory multitu io)
fail() {
  failures+=("$1:$2")
  stage_failures+=("$2")
  echo "MINIAS_RUNTIME_LADDER_STAGE=FAIL arch=$arch stage=$1 reason=$2" >&2
}

# CI logs must expose the *actual first unsupported GCC instruction* for
# every failing TU. Preserve complete stderr in artifacts, but print a small
# bounded excerpt so the next repair batch does not need artifact downloads.
diagnostic_log() {
  local step=$1 path=$2
  if test -s "$path"; then
    printf 'MINIAS_RUNTIME_LADDER_DIAGNOSTIC arch=%s stage=%s step=%s file=%s\n' \
      "$arch" "$stage" "$step" "$path" >&2
    sed -n '1,20p' "$path" >&2
  fi
}

for stage in "${stages[@]}"; do
  stage_dir="$work/$stage"
  mkdir -p "$stage_dir/asm" "$stage_dir/gnu" "$stage_dir/mini" "$stage_dir/evidence"
  stage_failures=()
  # A rerun must never accidentally inherit an earlier successful execution.
  rm -f "$stage_dir/evidence/programs.sha256"
  for flavor in gnu mini; do
    rm -f "$stage_dir/$flavor/program" \
      "$stage_dir/evidence/$flavor-exit.txt" \
      "$stage_dir/evidence/$flavor-stdout.txt" \
      "$stage_dir/evidence/$flavor-stderr.txt" \
      "$stage_dir/evidence/$flavor-link.log"
  done
  units=()
  case "$stage" in
    exit)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/exit.c")
      opt=(-O0); expected=''
      ;;
    control)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/control.c")
      opt=(-O1); expected='LADDER_CONTROL value=165 status=PASS'
      ;;
    memory)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/memory.c")
      opt=(-O2); expected='LADDER_MEMORY sum=135 status=PASS'
      ;;
    multitu)
      sources=("$repo/tests/assembler/runtime-gcc-single-variable/main.c"
               "$repo/tests/assembler/runtime-gcc-single-variable/helper.c")
      opt=(-O1); expected='MINIAS_GCC_RUNTIME value=74 bias=11 length=10 status=PASS'
      ;;
    io)
      sources=("$repo/tests/assembler/runtime-gcc-ladder/io.c")
      opt=(-O2); expected='LADDER_IO sum=1015 status=PASS'
      ;;
  esac
  if [[ -n "$expected" ]]; then
    printf '%s\n' "$expected" >"$stage_dir/evidence/expected.stdout"
  else
    : >"$stage_dir/evidence/expected.stdout"
  fi

  : >"$stage_dir/evidence/sources.sha256"
  for source in "${sources[@]}"; do
    unit=$(basename "$source" .c)
    units+=("$unit")
    asm="$stage_dir/asm/$unit.s"
    rm -f "$asm" "$stage_dir/gnu/$unit.o" "$stage_dir/mini/$unit.o"
    if "$cc" "${gcc_flags[@]}" "${opt[@]}" -std=c11 -g0 \
         -fno-pie -fno-pic -Wall -Wextra -Werror -S "$source" -o "$asm" \
         >"$stage_dir/evidence/gcc-$unit.log" 2>&1 && test -s "$asm"; then
      sha256sum "$source" "$asm" >>"$stage_dir/evidence/sources.sha256"
    else
      fail "$stage" "gcc-S:$unit"
      diagnostic_log "gcc-S:$unit" "$stage_dir/evidence/gcc-$unit.log"
      continue
    fi
    if ! "$gas" "${gas_flags[@]}" -o "$stage_dir/gnu/$unit.o" "$asm" \
         >"$stage_dir/evidence/gnu-as-$unit.log" 2>&1; then
      fail "$stage" "gnu-as:$unit"
      diagnostic_log "gnu-as:$unit" "$stage_dir/evidence/gnu-as-$unit.log"
    fi
    if ! "$minias" "${mini_flags[@]}" -o "$stage_dir/mini/$unit.o" "$asm" \
         >"$stage_dir/evidence/mini-as-$unit.log" 2>&1; then
      fail "$stage" "mini-as:$unit"
      diagnostic_log "mini-as:$unit" "$stage_dir/evidence/mini-as-$unit.log"
    fi
  done

  # Force the GNU linker and genuine cross-CRT on both sides. Each flavor
  # attempts linkage independently even if its counterpart cannot build.
  for flavor in gnu mini; do
    objects=()
    for unit in "${units[@]}"; do
      if test -s "$stage_dir/$flavor/$unit.o"; then
        objects+=("$stage_dir/$flavor/$unit.o")
      fi
    done
    if (( ${#objects[@]} != ${#sources[@]} )); then
      fail "$stage" "missing-object:$flavor"
      continue
    fi
    program="$stage_dir/$flavor/program"
    rm -f "$program"
    if ! "$cc" "${gcc_flags[@]}" -static -no-pie -fuse-ld=bfd \
         -Wl,--fatal-warnings -Wl,-v -o "$program" "${objects[@]}" \
         >"$stage_dir/evidence/$flavor-link.log" 2>&1 ||
       ! test -s "$program"; then
      fail "$stage" "gnu-bfd-link:$flavor"
      diagnostic_log "gnu-bfd-link:$flavor" "$stage_dir/evidence/$flavor-link.log"
      continue
    fi
    if ! grep -Fq 'GNU ld' "$stage_dir/evidence/$flavor-link.log" ||
       ! "$readelf" -h "$program" \
            >"$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! grep -Fq "$machine" "$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! grep -Eq 'Type:[[:space:]]+EXEC' \
            "$stage_dir/evidence/$flavor-elf-header.txt" ||
       ! "$readelf" -l "$program" \
            >"$stage_dir/evidence/$flavor-program-headers.txt" ||
       grep -q INTERP "$stage_dir/evidence/$flavor-program-headers.txt"; then
      fail "$stage" "link-provenance-or-static:$flavor"
      continue
    fi
    result=0
    timeout --signal=TERM 30s "$qemu" "$program" \
      >"$stage_dir/evidence/$flavor-stdout.txt" \
      2>"$stage_dir/evidence/$flavor-stderr.txt" || result=$?
    printf '%s\n' "$result" >"$stage_dir/evidence/$flavor-exit.txt"
    if (( result != 0 )); then
      fail "$stage" "runtime-exit:$flavor:$result"
    fi
    if ! cmp -s "$stage_dir/evidence/expected.stdout" \
                 "$stage_dir/evidence/$flavor-stdout.txt"; then
      fail "$stage" "runtime-contract:$flavor"
    fi
    if test -s "$stage_dir/evidence/$flavor-stderr.txt"; then
      fail "$stage" "runtime-stderr:$flavor"
    fi
  done

  if test -f "$stage_dir/evidence/gnu-exit.txt" &&
     test -f "$stage_dir/evidence/mini-exit.txt"; then
    for channel in stdout stderr exit; do
      if ! cmp -s "$stage_dir/evidence/gnu-$channel.txt" \
                   "$stage_dir/evidence/mini-$channel.txt"; then
        fail "$stage" "runtime-difference:$channel"
      fi
    done
  else
    fail "$stage" 'missing-runtime-side'
  fi

  if test -s "$stage_dir/gnu/program" && test -s "$stage_dir/mini/program"; then
    sha256sum "$stage_dir/gnu/program" "$stage_dir/mini/program" \
      >"$stage_dir/evidence/programs.sha256"
  fi

  if (( ${#stage_failures[@]} == 0 )); then
    ((pass_count += 1))
    echo "MINIAS_RUNTIME_LADDER_STAGE=PASS arch=$arch stage=$stage gnu_bfd=2 run=2 output_equal=1 runner=$qemu"
  fi
done

printf '%s\n' "${failures[@]}" >"$work/evidence/failures.txt"
if (( ${#failures[@]} != 0 )); then
  echo "MINIAS_RUNTIME_LADDER=FAIL arch=$arch stages=$pass_count/${#stages[@]} reasons=${failures[*]}" | \
    tee "$work/evidence/summary.txt" >&2
  exit 1
fi
echo "MINIAS_RUNTIME_LADDER=PASS arch=$arch stages=${#stages[@]}/${#stages[@]} gnu_bfd=2_per_stage runtime=2_per_stage runner=$qemu" | \
  tee "$work/evidence/summary.txt"
