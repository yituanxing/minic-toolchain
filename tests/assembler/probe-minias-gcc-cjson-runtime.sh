#!/usr/bin/env bash
# Genuine upstream cJSON 1.7.19: GCC owns C compilation, GCC/GNU BFD owns
# CRT/static libc/libm/linking, QEMU owns target execution. Only GAS is
# replaced with MiniAS. Two independent real programs must behave identically.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-cjson-runtime.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-gcc-cjson-runtime/$arch"}
vendor="$repo/tests/vendor/cjson/upstream"
mkdir -p "$work/asm" "$work/gnu" "$work/mini" "$work/evidence"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; gas=${GNU_AS:-as}; readelf=${GNU_READELF:-readelf}
    qemu=${QEMU_USER:-qemu-x86_64}
    cc_flags=(); as_flags=(--64)
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    machine='Advanced Micro Devices X86-64' ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}; gas=${GNU_AS:-aarch64-linux-gnu-as}
    readelf=${GNU_READELF:-aarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-aarch64}
    cc_flags=(); as_flags=(-march=armv8-a)
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    machine='AArch64' ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}; gas=${GNU_AS:-riscv64-linux-gnu-as}
    readelf=${GNU_READELF:-riscv64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-riscv64}
    cc_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    as_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    machine='RISC-V' ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gas=${GNU_AS:-loongarch64-linux-gnu-as}
    readelf=${GNU_READELF:-loongarch64-linux-gnu-readelf}
    qemu=${QEMU_USER:-qemu-loongarch64}
    cc_flags=(-mabi=lp64d); as_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    machine='LoongArch' ;;
  *) echo "CJSON_MINIAS_REAL_RUNTIME=ERROR invalid_arch:$arch" >&2; exit 64 ;;
esac

for exe in "$cc" "$gas" "$readelf" "$qemu" "$minias" timeout sha256sum cmp; do
  command -v "$exe" >/dev/null || {
    echo "CJSON_MINIAS_REAL_RUNTIME=ERROR missing_tool:$exe" >&2
    exit 127
  }
done

"$cc" --version >"$work/evidence/cc-version.txt"
"$gas" --version >"$work/evidence/gas-version.txt"
"$minias" --version >"$work/evidence/minias-version.txt" 2>&1 || :
"$cc" -print-prog-name=ld.bfd >"$work/evidence/linker.txt"
printf 'sha=%s\narch=%s\nrunner=%s\n' "${GITHUB_SHA:-local-uncommitted}" \
  "$arch" "$qemu" >"$work/evidence/identity.txt"
: >"$work/evidence/sources.sha256"
: >"$work/evidence/failures.txt"

failures=()
declare -A gnu_ok=() mini_ok=() ran=()
failed() {
  failures+=("$1")
  printf 'CJSON_MINIAS_STAGE=FAIL arch=%s step=%s\n' "$arch" "$1" |
    tee -a "$work/evidence/failures.txt" >&2
}

# Keep complete compiler/assembler/linker diagnostics as CI artifacts. A
# bounded excerpt in the job log makes every failed TU independently visible
# so a bulk fix can address *all* blockers exposed by the current GCC run.
diagnostic_log() {
  local step=$1 path=$2
  if test -s "$path"; then
    printf 'CJSON_MINIAS_DIAGNOSTIC arch=%s step=%s file=%s\n' \
      "$arch" "$step" "$path" >&2
    sed -n '1,20p' "$path" >&2
  fi
}

# All six objects originate from *identical .s bytes*, emitted only once by
# the cross/host GNU GCC. No compiler, source or flags difference by flavor.
for unit in cjson harness upstream_test; do
  case "$unit" in
    cjson) src="$vendor/cJSON.c" ;;
    harness) src="$repo/tests/external/cjson/runtime_harness.c" ;;
    upstream_test) src="$vendor/test.c" ;;
  esac
  if "$cc" "${cc_flags[@]}" -O1 -g0 -fno-pie -fno-pic \
      -Wall -Wextra -Werror -I "$vendor" -S "$src" \
      -o "$work/asm/$unit.s" >"$work/evidence/gcc-$unit.log" 2>&1 &&
      test -s "$work/asm/$unit.s"; then
    sha256sum "$src" "$work/asm/$unit.s" >>"$work/evidence/sources.sha256"
  else
    failed "gcc-S:$unit"
    diagnostic_log "gcc-S:$unit" "$work/evidence/gcc-$unit.log"
    continue
  fi
  if "$gas" "${as_flags[@]}" -o "$work/gnu/$unit.o" \
      "$work/asm/$unit.s" >"$work/evidence/gnu-as-$unit.log" 2>&1 &&
      test -s "$work/gnu/$unit.o"; then
    gnu_ok[$unit]=1
  else
    failed "GNU-as:$unit"
    diagnostic_log "GNU-as:$unit" "$work/evidence/gnu-as-$unit.log"
  fi
  if "$minias" "${mini_flags[@]}" -o "$work/mini/$unit.o" \
      "$work/asm/$unit.s" >"$work/evidence/mini-as-$unit.log" 2>&1 &&
      test -s "$work/mini/$unit.o"; then
    mini_ok[$unit]=1
  else
    failed "MiniAS:$unit"
    diagnostic_log "MiniAS:$unit" "$work/evidence/mini-as-$unit.log"
  fi
done

# The full harness covers 24 API checks. The original upstream test.c
# actually prints parsed and serialized JSON, including nested arrays,
# Unicode/escaping and floating-point formats. Both are mandatory.
for workload in full_harness upstream_test; do
  case "$workload" in
    full_harness) driver=harness ;;
    upstream_test) driver=upstream_test ;;
  esac
  for flavor in gnu mini; do
    if [[ "$flavor" == gnu ]] &&
       [[ ${gnu_ok[cjson]:-0} != 1 || ${gnu_ok[$driver]:-0} != 1 ]]; then
      failed "missing-GNU-object:$workload"
      continue
    fi
    if [[ "$flavor" == mini ]] &&
       [[ ${mini_ok[cjson]:-0} != 1 || ${mini_ok[$driver]:-0} != 1 ]]; then
      failed "missing-MiniAS-object:$workload"
      continue
    fi
    program="$work/$flavor/$workload"
    if ! "$cc" "${cc_flags[@]}" -static -no-pie -fuse-ld=bfd \
        -Wl,--fatal-warnings -Wl,-v -o "$program" \
        "$work/$flavor/cjson.o" "$work/$flavor/$driver.o" -lm \
        >"$work/evidence/$flavor-$workload-link.log" 2>&1; then
      failed "GNU-BFD-link:$flavor:$workload"
      diagnostic_log "GNU-BFD-link:$flavor:$workload" \
        "$work/evidence/$flavor-$workload-link.log"
      continue
    fi
    if ! grep -Fq 'GNU ld' "$work/evidence/$flavor-$workload-link.log" ||
       ! "$readelf" -h "$program" \
          >"$work/evidence/$flavor-$workload-elf-header.txt" ||
       ! grep -Fq "$machine" \
          "$work/evidence/$flavor-$workload-elf-header.txt" ||
       ! "$readelf" -l "$program" \
          >"$work/evidence/$flavor-$workload-elf-phdr.txt" ||
       grep -q INTERP "$work/evidence/$flavor-$workload-elf-phdr.txt"; then
      failed "invalid-static-GNU-link:$flavor:$workload"
      continue
    fi
    if timeout --signal=TERM 60s "$qemu" "$program" \
        >"$work/evidence/$flavor-$workload-stdout.txt" \
        2>"$work/evidence/$flavor-$workload-stderr.txt"; then
      exit_status=0
    else
      exit_status=$?
    fi
    printf '%s\n' "$exit_status" \
      >"$work/evidence/$flavor-$workload-exit.txt"
    ran["$flavor-$workload"]=1
    if (( exit_status != 0 )); then
      failed "runtime-exit:$flavor:$workload:$exit_status"
    fi
  done
  if [[ ${ran[gnu-$workload]:-0} == 1 &&
        ${ran[mini-$workload]:-0} == 1 ]]; then
    for channel in stdout stderr exit; do
      if ! cmp -s "$work/evidence/gnu-$workload-$channel.txt" \
                   "$work/evidence/mini-$workload-$channel.txt"; then
        failed "runtime-output-diff:$workload:$channel"
      fi
    done
    if [[ "$workload" == upstream_test ]]; then
      if ! grep -Fxq 'Version: 1.7.19' \
            "$work/evidence/mini-$workload-stdout.txt" ||
         ! test -s "$work/evidence/mini-$workload-stdout.txt"; then
        failed 'upstream-JSON-output-contract'
      fi
    fi
  fi
done

if (( ${#failures[@]} > 0 )); then
  printf 'CJSON_MINIAS_REAL_RUNTIME=FAIL arch=%s count=%s failures=%s\n' \
    "$arch" "${#failures[@]}" "${failures[*]}" >&2
  exit 1
fi
printf 'CJSON_MINIAS_REAL_RUNTIME=PASS arch=%s units=3 workloads=2 GCC_S=1 MiniAS_only=1 GNU_BFD_static_libc_libm=1 engine=%s GNU_output_identical=1\n' \
  "$arch" "$qemu"
