#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
minic=${MINIC:-}
keep=${MINIC_KEEP_INTERMEDIATES:-0}
trace=${MINIC_KBUILD_TRACE:-}

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

cleanup() {
  if [[ "$keep" != 1 ]]; then
    [[ "$source_file" == *.i ]] || rm -f "$preprocessed"
    rm -f "$assembly" "$minic_stdout" "$minic_stderr"
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
  "$real_cc" "${pp_args[@]}" -E -P -MT "$output_file" "$source_file" -o "$preprocessed"
else
  preprocessed=$source_file
fi

[[ -z "$trace" ]] || printf 'minic input=%q output=%q\n' "$preprocessed" "$assembly" >>"$trace"
set +e
CORE_FAST_TRACE=${CORE_FAST_TRACE:-1} "$minic" -S "$preprocessed" -o "$assembly"   >"$minic_stdout" 2>"$minic_stderr"
status=$?
set -e
if (( status != 0 )); then
  echo "MINIC_KBUILD_BLOCKER source=$source_file output=$output_file status=$status" >&2
  sed -n '1,200p' "$minic_stderr" >&2 || true
  exit "$status"
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

[[ -z "$trace" ]] || printf 'pass source=%q output=%q\n' "$source_file" "$output_file" >>"$trace"
