#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
minic=${MINIC:-}
keep=${MINIC_KEEP_INTERMEDIATES:-0}
trace=${MINIC_KBUILD_TRACE:-}
gcc_vdso=${MINIC_KBUILD_GCC_VDSO:-0}
gcc_purgatory=${MINIC_KBUILD_GCC_PURGATORY:-0}
# Opt-in *compiler only* RV64I Kbuild audit. Preserve Kbuild's original GNU
# assembler ISA flags for explicit user/kernel inline M/A/CSR instructions.
# The linker must separately close compiler-generated software arithmetic.
rv64i_origin_audit=${MINIC_KBUILD_RV64I_ORIGIN_AUDIT:-0}
# Use the SAME Kbuild GNU-E -> MiniC-S -> GNU-AS route for RV32I.
# Kbuild owns -march=rv32ima for its explicit kernel ASM; only the MiniC
# compiler-owned stream is RV32I/ILP32. Neither route is an RV32IMAC backend.
rv32i_origin_audit=${MINIC_KBUILD_RV32I_ORIGIN_AUDIT:-0}
if [[ "$rv64i_origin_audit" == 1 && "$rv32i_origin_audit" == 1 ]]; then
  echo 'MINIC_KBUILD_ERROR conflicting_RV64I_RV32I_routes' >&2
  exit 2
fi

if [[ -z "$minic" ]]; then
  echo "MINIC_KBUILD_ERROR MINIC is not set" >&2
  exit 2
fi

if [[ -n "$trace" ]]; then
  mkdir -p "$(dirname "$trace")"
  {
    printf 'argv'
    printf ' %q' "$@"
    printf '\n'
  } >>"$trace"
fi

args=("$@")
source_file=
output_file=
compile_only=0
preprocess_only=0
assembly_only=0

for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -c) compile_only=1 ;;
    -E) preprocess_only=1 ;;
    -S) assembly_only=1 ;;
    -o)
      if (( i + 1 < ${#args[@]} )); then
        output_file=${args[i+1]}
        ((++i))
      fi
      ;;
    *.c|*.i|*.S|*.s)
      source_file=$arg
      ;;
  esac
done

# Compiler feature probes, preprocessing requests, final links, and assembly
# sources remain external-toolchain responsibilities in the active compiler
# milestone. Only ordinary C/.i -> relocatable object compilation is replaced.
if (( preprocess_only )) || (( assembly_only )) || (( ! compile_only )) ||
   [[ -z "$source_file" ]] || [[ "$source_file" == *.S ]] || [[ "$source_file" == *.s ]]; then
  [[ -z "$trace" ]] || printf 'delegate real_cc\n' >>"$trace"
  exec "$real_cc" "$@"
fi

if [[ "$source_file" != *.c && "$source_file" != *.i ]]; then
  [[ -z "$trace" ]] || printf 'delegate unknown-source real_cc\n' >>"$trace"
  exec "$real_cc" "$@"
fi

# Runtime bring-up may opt into a deliberately tiny vDSO exception.  MiniC V1
# currently emits a much larger native RISC-V vDSO than the upstream linker
# script's 0x800 metadata reservation accepts.  Keep this exception explicit,
# traceable, and restricted to the two C translation units in native vDSO.
# Strict all-MiniC validation does not set MINIC_KBUILD_GCC_VDSO.
if [[ "$gcc_vdso" == 1 && "$source_file" == */arch/riscv/kernel/vdso/vgettimeofday.c ||
      "$gcc_vdso" == 1 && "$source_file" == */arch/riscv/kernel/vdso/hwprobe.c ]]; then
  [[ -z "$trace" ]] ||
    printf 'delegate-vdso source=%q output=%q real_cc=%q\n' "$source_file" "$output_file" "$real_cc" >>"$trace"
  exec "$real_cc" "$@"
fi

# The RISC-V kexec purgatory is another deliberately restricted freestanding
# build domain.  Its four C objects are linked into an isolated relocatable
# payload with a tiny symbol closure, not into the normal kernel object graph.
# Runtime bring-up may delegate exactly those output objects to GCC while the
# compiler-side purgatory defect is tracked separately.  Strict/final all-MiniC
# validation does not set MINIC_KBUILD_GCC_PURGATORY.
if [[ "$gcc_purgatory" == 1 && "$output_file" == arch/riscv/purgatory/*.o &&
      ( "$source_file" == *.c || "$source_file" == *.i ) ]]; then
  [[ -z "$trace" ]] ||
    printf 'delegate-purgatory source=%q output=%q real_cc=%q\n' "$source_file" "$output_file" "$real_cc" >>"$trace"
  exec "$real_cc" "$@"
fi

if [[ -z "$output_file" ]]; then
  echo "MINIC_KBUILD_ERROR missing -o for source=$source_file" >&2
  exit 2
fi

mkdir -p "$(dirname "$output_file")"
base=${output_file%.*}.minic-stage2
preprocessed=${base}.i
assembly=${base}.s
minic_stdout=${base}.minic.stdout
minic_stderr=${base}.minic.stderr
preserve_failure=${MINIC_PRESERVE_FAILURE_INPUTS:-0}
failed_i=${base}.failed.i
failed_stderr=${base}.failed.stderr

# When Kbuild receives many individual goals in one parallel invocation,
# recursive make can issue the same C owner twice. Serialize that owner's
# GNU-E -> MiniC-S -> GNU-AS -> cleanup transaction, without slowing down
# other C owners in the seven-way sweep. In particular, no second invocation
# may truncate *.i while MiniC is still reading it or remove the first
# invocation's intermediate in its EXIT trap.
exec {minic_stage2_lock_fd}>"${base}.lock"
flock -x "$minic_stage2_lock_fd"

# Each invocation owns this target's failure evidence. Never reuse stale
# failed inputs when a later run succeeds.
if [[ "$preserve_failure" == 1 ]]; then
  rm -f "$failed_i" "$failed_stderr"
fi

cleanup() {
  local rc=$?
  if [[ "$keep" != 1 ]]; then
    if [[ "$preserve_failure" == 1 && "$rc" -ne 0 ]]; then
      # Keep ONLY failed TUs: a 2064-owner sweep must not retain 2064 .i files.
      # GNU-preprocessed .i and MiniC diagnostics stay paired to this owner.
      if [[ -s "$preprocessed" ]]; then
        cp -a -- "$preprocessed" "$failed_i"
      fi
      if [[ -s "$minic_stderr" ]]; then
        cp -a -- "$minic_stderr" "$failed_stderr"
      fi
    fi
    [[ "$source_file" == *.i ]] || rm -f "$preprocessed"
    rm -f "$assembly" "${base}.perf-baseline.s" "$minic_stdout" "$minic_stderr"
  fi
}
trap cleanup EXIT

if [[ "$source_file" == *.c ]]; then
  pp_args=()
  for ((i=0; i<${#args[@]}; ++i)); do
    arg=${args[i]}
    case "$arg" in
      -c|-E|-S)
        ;;
      -o)
        ((++i))
        ;;
      "$source_file")
        ;;
      *)
        pp_args+=("$arg")
        ;;
    esac
  done

  [[ -z "$trace" ]] || printf 'preprocess source=%q output=%q\n' "$source_file" "$preprocessed" >>"$trace"
  # Keep the historical GNU-E route unchanged unless the exact native
  # dynamic-header gate is enabled. This is a four-owner, opt-in promotion,
  # not a global Kbuild compiler-selector replacement.
  "$real_cc" "${pp_args[@]}" -E -P -MT "$output_file" "$source_file" -o "$preprocessed"
  if [[ -n "${MINIC_KBUILD_MINIPP:-}" ]]; then
    : "${MINIPP_LINUX_SRC:?native PP needs the original Linux source tree}"
    [[ -x "$MINIC_KBUILD_MINIPP" ]] || {
      echo "MINIPP_DYNAMIC_HEADER_PP=FAIL missing_minipp:$MINIC_KBUILD_MINIPP" >&2; exit 9;
    }
    root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
    native_pp_args=()
    pp_context=()
    for ((i=0; i<${#args[@]}; ++i)); do
      arg=${args[i]}
      case "$arg" in
        -D|-U|-I|-isystem|-include)
          ((i+1<${#args[@]})) || { echo "MINIPP_DYNAMIC_HEADER_PP=FAIL truncated:$arg" >&2; exit 9; }
          native_pp_args+=("$arg" "${args[i+1]}")
          ((i+=1))
          ;;
        -D*|-U*|-I*|-nostdinc|-isystem*|-include*|-fmacro-prefix-map=*)
          # Preserve Kbuild's real __FILE__ mapping, not absolute host paths.
          native_pp_args+=("$arg") ;;
        -march=*|-mabi=*|-std=*|-O0|-O1|-O2|-O3|-Og|-Os|-Oz|-Ofast|-ffreestanding|-fhosted|-fno-builtin|-fno-builtin-wcslen|-fbuiltin|-fshort-wchar|-funsigned-char|-fsigned-char|-fno-PIE|-fPIE|-fno-pie|-fpie|-fPIC|-fpic|-fno-PIC|-fno-pic|-mcmodel=*)
          # Optimization and hostedness control GCC built-in macros such as
          # __OPTIMIZE__ / __STDC_HOSTED__, hence Linux BUILD_BUG_ON bodies.
          # Omitting them made MiniPP and normalized GNU agree while both
          # differed semantically from Kbuild's authoritative GNU -E.
          pp_context+=("$arg") ;;
        -imacros|-iquote|-idirafter|-isysroot|-iprefix|-iwithprefix|-iwithprefixbefore)
          echo "MINIPP_DYNAMIC_HEADER_PP=FAIL unsupported_kbuild_pp_option:$arg" >&2; exit 9 ;;
      esac
    done
    native_macro="${base}.native-pp-predefines.h"
    native_reference="${base}.native-pp-reference.i"
    native_candidate="${base}.native-pp-candidate.i"
    MINIPP_LINUX_SRC="$MINIPP_LINUX_SRC" MINIPP_PREDEFINES_OUT="$native_macro" REAL_CC="$real_cc" \
      bash "$root/tests/preprocessor/build_reference_predefines.sh" "${pp_context[@]}" \
      >"${base}.native-pp-predefines.log"
    common_pp=(-E -P -undef -x c -include "$native_macro" "${native_pp_args[@]}" "$source_file")
    "$real_cc" "${common_pp[@]}" -o "$native_reference"
    "$MINIC_KBUILD_MINIPP" "${common_pp[@]}" -o "$native_candidate"
    if ! cmp -s "$native_reference" "$native_candidate"; then
      diff -u "$native_reference" "$native_candidate" >"${base}.native-pp.diff" || true
      echo "MINIPP_DYNAMIC_HEADER_PP=FAIL native_vs_reference source=$source_file output=$output_file" >&2
      exit 9
    fi
    # The exact legacy GNU-E output is also authoritative. Prevent a
    # false pass when normalized GNU and MiniPP agree but their flags omit
    # a source-dependent Kbuild preprocessor effect.
    if ! cmp -s "$preprocessed" "$native_candidate"; then
      diff -u "$preprocessed" "$native_candidate" >"${base}.native-pp-legacy.diff" || true
      echo "MINIPP_DYNAMIC_HEADER_PP=FAIL native_vs_legacy_gnu source=$source_file output=$output_file" >&2
      sed -n '1,100p' "${base}.native-pp-legacy.diff" >&2
      exit 9
    fi
    cp -a "$native_candidate" "$preprocessed"
    printf 'MINIPP_DYNAMIC_HEADER_PP=PASS source=%s output=%s mini_sha=%s legacy_gnu_equal=true\n' \
      "$source_file" "$output_file" "$(sha256sum "$preprocessed" | cut -d' ' -f1)"
    # Preserve the actual MiniPP-produced intermediate when capture is enabled.
    if [[ "$keep" != 1 ]]; then
      rm -f "$native_macro" "$native_macro.attribute.names" "$native_macro.builtin.names" \
        "$native_reference" "$native_candidate" "${base}.native-pp-predefines.log"
    fi
  fi
else
  preprocessed=$source_file
fi

[[ -z "$trace" ]] || printf 'minic input=%q output=%q\n' "$preprocessed" "$assembly" >>"$trace"
minic_target_args=()
if [[ "$rv64i_origin_audit" == 1 ]]; then
  # Audit is explicit, never silently enabled for production Kbuild. MiniC
  # emits strict RV64I and brackets the original user ASM for provenance.
  # GNU assembler still gets the ORIGINAL -march/-mabi Kbuild arguments:
  # removing M/A here would incorrectly reject Linux's own inline ASM.
  minic_target_args=(-march=rv64i -mabi=lp64)
  [[ -z "$trace" ]] || printf 'minic rv64i_origin_audit=1 source=%q\n' "$preprocessed" >>"$trace"
elif [[ "$rv32i_origin_audit" == 1 ]]; then
  # Kbuild's 32-bit ILP32 baseline uses rv32ima for kernel-owned atomic/CSR
  # assembly. Do not pass those ISA extensions through to the C code generator.
  has_32_abi=0
  has_32_isa=0
  for arg in "${args[@]}"; do
    [[ "$arg" != -mabi=ilp32 ]] || has_32_abi=1
    case "$arg" in -march=rv32ima*) has_32_isa=1 ;; esac
  done
  if [[ "$has_32_abi" != 1 || "$has_32_isa" != 1 ]]; then
    echo "MINIC_KBUILD_ERROR not_original_RV32I_Linux_Kbuild source=$source_file" >&2
    exit 2
  fi
  minic_target_args=(--target=riscv32-linux -march=rv32i -mabi=ilp32)
  [[ -z "$trace" ]] ||
    printf 'minic rv32i_origin_audit=1 isa=rv32i abi=ilp32 source=%q\n' "$preprocessed" >>"$trace"
fi
# GNU -E freezes tokens, not invocation-scoped C ABI or relocation policy.
# Preserve these original Kbuild flags for EVERY MiniC compiler route.  In
# particular, RV64 EFI's L"SecureBoot" must remain 16-bit -fshort-wchar;
# previously only RV32 forwarded this option and all RV64 EFI callers failed
# their correctly strict indirect function-call type check.
minic_language_args=()
for arg in "${args[@]}"; do
  case "$arg" in
    -fshort-wchar|-fno-short-wchar|-fsigned-char|-funsigned-char|\
    -fno-signed-char|-fno-unsigned-char|-ffunction-sections|\
    -fno-function-sections|-fPIE|-fpie|-fPIC|-fpic|\
    -fno-pie|-fno-PIE|-fno-pic|-fno-PIC)
      minic_language_args+=("$arg") ;;
  esac
done
set +e
if [[ -n "${MINIC_PERF_AB_BASELINE:-}" ]]; then
  # Strict opt-in: time old and new compilers on the SAME GNU-generated .i,
  # preserve normal candidate assembler output, and require identical .s.
  python3 "${BASH_SOURCE[0]%/*}/../../../tools/ci/linux-perf-2064-paired-compile-v1.py" pair \
    --baseline "$MINIC_PERF_AB_BASELINE" --candidate "$minic" \
    --input "$preprocessed" --assembly "$assembly" \
    --baseline-assembly "${base}.perf-baseline.s" \
    --stdout "$minic_stdout" --stderr "$minic_stderr" \
    --log "${MINIC_PERF_AB_LOG:?}" --owner "$output_file"
  status=$?
else
  if [[ "$rv64i_origin_audit" == 1 ]]; then
    MINIC_RV64I_ASM_AUDIT=1 CORE_FAST_TRACE=${CORE_FAST_TRACE:-1} \
      "$minic" "${minic_target_args[@]}" "${minic_language_args[@]}" \
      -S "$preprocessed" -o "$assembly" \
      >"$minic_stdout" 2>"$minic_stderr"
  elif [[ "$rv32i_origin_audit" == 1 ]]; then
    CORE_FAST_TRACE=${CORE_FAST_TRACE:-1} \
      "$minic" "${minic_target_args[@]}" "${minic_language_args[@]}" \
      -S "$preprocessed" -o "$assembly" \
      >"$minic_stdout" 2>"$minic_stderr"
  else
    CORE_FAST_TRACE=${CORE_FAST_TRACE:-1} \
      "$minic" "${minic_language_args[@]}" -S "$preprocessed" -o "$assembly" \
      >"$minic_stdout" 2>"$minic_stderr"
  fi
  status=$?
fi
set -e
if (( status != 0 )); then
  echo "MINIC_KBUILD_BLOCKER source=$source_file output=$output_file status=$status" >&2
  sed -n '1,200p' "$minic_stderr" >&2 || true
  exit "$status"
fi

# Opt-in per-TU Boolean proof counters: capture before Kbuild cleanup,
# without retaining multi-gigabyte .i/.s intermediates on the runner.
if [[ -n "${MINIC_BOOLEAN_DOMAIN_COUNTS_LOG:-}" ]]; then
  metric=$(grep -E '^MINIC_BOOLEAN_DOMAIN queries=[0-9]+ builds=[0-9]+ fallbacks=[0-9]+$' "$minic_stderr" | tail -n 1 || true)
  if [[ -z "$metric" ]]; then
    echo "MINIC_BOOLEAN_METRICS=FAIL missing_compiler_counter owner=$output_file" >&2
    exit 11
  fi
  {
    flock 9
    printf '%s\t%s\n' "$output_file" "$metric" >&9
  } 9>>"$MINIC_BOOLEAN_DOMAIN_COUNTS_LOG"
fi

# Count integer-owner filtering during each existing real MiniC Kbuild compile.
# Opt-in only; no separate compilation or new runner.
if [[ -n "${MINIC_INTEGER_OWNER_COUNTS_LOG:-}" ]]; then
  metric=$(grep -E '^MINIC_INTEGER_OWNER_FILTER ready=[01] candidates=[0-9]+ skipped_other_owner=[0-9]+ fallback=[01]$' "$minic_stderr" | tail -n 1 || true)
  if [[ -z "$metric" ]]; then
    echo "MINIC_INTEGER_OWNER_METRICS=FAIL missing_compiler_counter owner=$output_file" >&2
    exit 11
  fi
  {
    flock 9
    printf '%s\t%s\n' "$output_file" "$metric" >&9
  } 9>>"$MINIC_INTEGER_OWNER_COUNTS_LOG"
fi

asm_args=()
for arg in "${args[@]}"; do
  case "$arg" in
    -march=*|-mabi=*|-mcmodel=*|-mstrict-align|-mno-strict-align|-mno-save-restore|-mno-relax|-Wa,*)
      asm_args+=("$arg")
      ;;
  esac
done

[[ -z "$trace" ]] || printf 'assemble input=%q output=%q\n' "$assembly" "$output_file" >>"$trace"
"$real_cc" "${asm_args[@]}" -x assembler -c "$assembly" -o "$output_file"


# A route record alone cannot prove that the final object was not replaced
# after compilation. Record the EXACT object bytes produced through MiniC
# and GNU as, before emitting the legacy success marker. Both writes are
# single-line O_APPEND operations, safe for different concurrently built TUs.
if [[ -n "${MINIC_KBUILD_OBJECT_HASH_TRACE:-}" ]]; then
  object_hash=$(sha256sum -- "$output_file" | cut -d' ' -f1)
  printf '%s  %s\n' "$object_hash" "$output_file" >>"$MINIC_KBUILD_OBJECT_HASH_TRACE"
fi
if [[ -n "${MINIC_KBUILD_SUCCESS_TRACE:-}" ]]; then
  printf '%s\n' "$output_file" >>"$MINIC_KBUILD_SUCCESS_TRACE"
fi
[[ -z "$trace" ]] || printf 'pass source=%q output=%q\n' "$source_file" "$output_file" >>"$trace"
