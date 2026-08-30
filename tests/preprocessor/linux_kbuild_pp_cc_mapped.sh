#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
reference_cpp=${REFERENCE_CPP:-riscv64-linux-gnu-gcc}
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

linux_src=${MINIPP_LINUX_SRC:?MINIPP_LINUX_SRC is not set}
linux_out=${MINIPP_LINUX_OUT:?MINIPP_LINUX_OUT is not set}
rel_source=$source_file
case "$rel_source" in
  "$linux_src"/*) rel_source=${rel_source#"$linux_src"/} ;;
  ./*) rel_source=${rel_source#./} ;;
esac

select_tsv=${MINIPP_LINUX_SELECT_TSV:?MINIPP_LINUX_SELECT_TSV is not set}
if [[ ! -f "$select_tsv" ]]; then
  printf 'minipp-linux-mapped-wrapper: selector-not-found:%s\n' "$select_tsv" >&2
  exit 2
fi
if [[ -z "$output_file" ]]; then
  exec "$real_cc" "$@"
fi

if [[ "$output_file" == /* ]]; then
  target_abs=$(realpath -m "$output_file")
else
  target_abs=$(realpath -m "$PWD/$output_file")
fi
out_abs=$(realpath -m "$linux_out")
case "$target_abs" in
  "$out_abs"/*)
    rel_target=${target_abs#"$out_abs"/}
    ;;
  *)
    exec "$real_cc" "$@"
    ;;
esac

if ! awk -F '\t' -v target="$rel_target" -v source="$rel_source" '
  NR > 1 && $2 == target && $3 == source { found=1; exit }
  END { exit found ? 0 : 1 }
' "$select_tsv"; then
  exec "$real_cc" "$@"
fi

mkdir -p "$work" "$(dirname "$trace")"
key=${rel_target//\//__}__${rel_source//\//__}
key=${key//./_}
ref_i="$work/$key.gcc.i"
mini_i="$work/$key.mini.i"
mini_err="$work/$key.mini.err"
diff_file="$work/$key.diff"

pp_args=()
predef_args=()
for ((i=0; i<${#args[@]}; ++i)); do
  arg=${args[i]}
  case "$arg" in
    -D|-U|-I|-isystem|-include)
      pp_args+=("$arg")
      if (( i + 1 < ${#args[@]} )); then
        ((++i))
        pp_args+=("${args[i]}")
      fi
      ;;
    -D*|-U*|-I*|-nostdinc|-isystem*|-include*)
      pp_args+=("$arg")
      ;;
    -march=*|-mabi=*|-std=*|-fshort-wchar|-funsigned-char|-fsigned-char|-fno-PIE|-fPIE|-fno-pie|-fpie|-fPIC|-fpic)
      predef_args+=("$arg")
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

predefines="$work/riscv64-gcc-predefines.h"
if [[ ! -s "$predefines" ]]; then
  tmp_predefines="$predefines.tmp"
  "$real_cc" "${predef_args[@]}" -dM -E -x c /dev/null >"$tmp_predefines"

  cat >>"$tmp_predefines" <<'EOF'
#define __MINIPP_PP_CAT2(a, b) a##b
#define __MINIPP_PP_CAT(a, b) __MINIPP_PP_CAT2(a, b)
EOF

  for capability in attribute builtin; do
    names="$work/has-$capability.names"
    {
      grep -RhoE "__has_${capability}\\([[:space:]]*[A-Za-z_][A-Za-z0-9_]*" \
        "$linux_src/include" "$linux_src/arch/riscv/include" 2>/dev/null || true
    } |
      sed -E "s/^__has_${capability}\\([[:space:]]*//" |
      LC_ALL=C sort -u >"$names"

    while IFS= read -r name; do
      [[ -n "$name" ]] || continue
      value=$(
        printf '#if __has_%s(%s)\n1\n#else\n0\n#endif\n' \
          "$capability" "$name" |
          "$real_cc" "${predef_args[@]}" -E -P -x c - |
          tail -n 1
      )
      upper=${capability^^}
      printf '#define __MINIPP_HAS_%s_%s %s\n' \
        "$upper" "$name" "$value" >>"$tmp_predefines"
    done <"$names"

    upper=${capability^^}
    printf '#define __has_%s(x) __MINIPP_PP_CAT(__MINIPP_HAS_%s_, x)\n' \
      "$capability" "$upper" >>"$tmp_predefines"
  done

  mv "$tmp_predefines" "$predefines"
fi
pp_args=(-include "$predefines" "${pp_args[@]}")

{
  printf 'TARGET %q SOURCE %q\n' "$rel_target" "$source_file"
  printf 'PREDEF_ARGS'
  printf ' %q' "${predef_args[@]}"
  printf '\n'
  printf 'PP_ARGS'
  printf ' %q' "${pp_args[@]}"
  printf '\n'
} >>"$trace"

"$reference_cpp" -E -P -undef -x c "${pp_args[@]}" "$source_file" -o "$ref_i"

set +e
"$minipp" -E -P -undef -x c "${pp_args[@]}" "$source_file" -o "$mini_i" 2>"$mini_err"
mini_status=$?
set -e

if (( mini_status != 0 )); then
  reason=$(head -n 1 "$mini_err" | tr '\n' ' ')
  printf 'RESULT target=%q source=%q status=MINIPP_FAIL rc=%s reason=%q\n' \
    "$rel_target" "$source_file" "$mini_status" "$reason" >>"$trace"
elif cmp -s "$ref_i" "$mini_i"; then
  printf 'RESULT target=%q source=%q status=EXACT\n' \
    "$rel_target" "$source_file" >>"$trace"
else
  diff -u "$ref_i" "$mini_i" >"$diff_file" || true
  first=$(sed -n '1,8p' "$diff_file" | tr '\n' ' ')
  printf 'RESULT target=%q source=%q status=DIFF reason=%q\n' \
    "$rel_target" "$source_file" "$first" >>"$trace"
fi

exec "$real_cc" "$@"
