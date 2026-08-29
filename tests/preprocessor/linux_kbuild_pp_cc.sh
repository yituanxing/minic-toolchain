#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
reference_cpp=${REFERENCE_CPP:-gcc}
minipp=${MINIPP:?MINIPP is not set}
trace=${MINIPP_LINUX_TRACE:?MINIPP_LINUX_TRACE is not set}
work=${MINIPP_LINUX_WORK:?MINIPP_LINUX_WORK is not set}

args=("$@")
source_file=
output_file=
compile_only=0

for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -c) compile_only=1 ;;
    -o)
      if (( i + 1 < ${#args[@]} )); then
        output_file=${args[i+1]}
        ((++i))
      fi
      ;;
    *.c) source_file=$arg ;;
  esac
done

if (( ! compile_only )) || [[ -z "$source_file" ]]; then
  exec "$real_cc" "$@"
fi

case "$source_file" in
  */init/main.c|init/main.c|*/kernel/configs.c|kernel/configs.c)
    ;;
  *)
    exec "$real_cc" "$@"
    ;;
esac

mkdir -p "$work" "$(dirname "$trace")"
key=${source_file//\//__}
key=${key//./_}
ref_i="$work/$key.gcc.i"
mini_i="$work/$key.mini.i"
mini_err="$work/$key.mini.err"
diff_file="$work/$key.diff"

pp_args=()
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -D*|-U*|-I*|-nostdinc)
      pp_args+=("$arg")
      ;;
    -D|-U|-I|-isystem|-include)
      pp_args+=("$arg")
      if (( i + 1 < ${#args[@]} )); then
        ((++i))
        pp_args+=("${args[i]}")
      fi
      ;;
    -isystem*|-include*)
      pp_args+=("$arg")
      ;;
    -Wp,*|-MMD|-MD|-MP)
      ;;
    -MF|-MT|-MQ)
      ((++i))
      ;;
    *)
      ;;
  esac
done

{
  printf 'SOURCE %q\n' "$source_file"
  printf 'PP_ARGS'
  printf ' %q' "${pp_args[@]}"
  printf '\n'
} >>"$trace"

"$reference_cpp" -E -P -undef -x c "${pp_args[@]}"   "$source_file" -o "$ref_i"

set +e
"$minipp" -E -P -undef -x c "${pp_args[@]}"   "$source_file" -o "$mini_i" 2>"$mini_err"
mini_status=$?
set -e

if (( mini_status != 0 )); then
  reason=$(head -n 1 "$mini_err" | tr '\n' ' ')
  printf 'RESULT source=%q status=MINIPP_FAIL rc=%s reason=%q\n'     "$source_file" "$mini_status" "$reason" >>"$trace"
elif cmp -s "$ref_i" "$mini_i"; then
  printf 'RESULT source=%q status=EXACT\n' "$source_file" >>"$trace"
else
  diff -u "$ref_i" "$mini_i" >"$diff_file" || true
  first=$(sed -n '1,8p' "$diff_file" | tr '\n' ' ')
  printf 'RESULT source=%q status=DIFF reason=%q\n'     "$source_file" "$first" >>"$trace"
fi

exec "$real_cc" "$@"
