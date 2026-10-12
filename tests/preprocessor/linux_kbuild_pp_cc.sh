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
rel_source=$source_file
case "$rel_source" in
  "$linux_src"/*) rel_source=${rel_source#"$linux_src"/} ;;
  ./*) rel_source=${rel_source#./} ;;
esac

select_file=${MINIPP_LINUX_SELECT_FILE:-}
if [[ -n "$select_file" ]]; then
  if [[ ! -f "$select_file" ]]; then
    printf 'minipp-linux-wrapper: selector-not-found:%s\n' "$select_file" >&2
    exit 2
  fi
  if ! grep -Fxq -- "$rel_source" "$select_file"; then
    if [[ "${MINIPP_LINUX_SELECT_MODE:-}" == strict ]]; then
      printf 'MINIPP_NATIVE_FULL_SELECTION=FAIL source=%q normalized=%q\n' "$source_file" "$rel_source" >&2
      exit 2
    fi
    exec "$real_cc" "$@"
  fi
else
  case "$rel_source" in
    init/main.c|kernel/configs.c)
      ;;
    *)
      exec "$real_cc" "$@"
      ;;
  esac
fi

mkdir -p "$work" "$(dirname "$trace")"
key=${source_file//\//__}
key=${key//./_}
# Full-cohort parallel replay hashes each actual .o identity into a
# bounded, collision-free input key; avoid >255-byte filename components.
if [[ -n "${MINIPP_LINUX_INPUT_KEY:-}" ]]; then
  key=$MINIPP_LINUX_INPUT_KEY
fi
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
# Native-substitution mode requires one independent GCC macro context
# per TU, rather than racing on the observation-mode shared tmp file.
if [[ "${MINIPP_LINUX_SUBSTITUTE:-false}" == "true" ]]; then
  predefines="$work/$key.riscv64-gcc-predefines.h"
fi
# Prebuild GNU predefined-macro contexts once per distinct GCC target
# flags across the full Linux TU cohort; no per-file capability grep
# or concurrent races on a shared GCC predefines file.
if [[ -n "${MINIPP_LINUX_PREDEFINES_FILE:-}" ]]; then
  predefines=$MINIPP_LINUX_PREDEFINES_FILE
  if [[ ! -s "$predefines" ]]; then
    printf 'MINIPP_NATIVE_FULL_PREDEFINES=FAIL file=%q\n' "$predefines" >&2
    exit 2
  fi
fi
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
  printf 'SOURCE %q\n' "$source_file"
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
  printf 'RESULT source=%q status=MINIPP_FAIL rc=%s reason=%q\n'     "$source_file" "$mini_status" "$reason" >>"$trace"
elif cmp -s "$ref_i" "$mini_i"; then
  printf 'RESULT source=%q status=EXACT\n' "$source_file" >>"$trace"
else
  diff -u "$ref_i" "$mini_i" >"$diff_file" || true
  first=$(sed -n '1,8p' "$diff_file" | tr '\n' ' ')
  printf 'RESULT source=%q status=DIFF reason=%q\n'     "$source_file" "$first" >>"$trace"
fi


# Fail-closed native MiniPP production substitution; the observational
# historical oracle remains the default when this mode is unset.
if [[ "${MINIPP_LINUX_SUBSTITUTE:-false}" == "true" ]]; then
  if (( mini_status != 0 )) || ! cmp -s "$ref_i" "$mini_i"; then
    printf 'MINIPP_NATIVE_REPLACEMENT=FAIL source=%q reason=non-exact-or-error\n' "$source_file" >&2
    exit 1
  fi
  dep_args=("${args[@]}")
  object_args=("${args[@]}")
  saw_c=0
  saw_input=0
  for ((i=0; i<${#args[@]}; ++i)); do
    if [[ "${dep_args[i]}" == -c ]]; then
      dep_args[i]=-E
      saw_c=1
    elif [[ "${dep_args[i]}" == -o && $((i+1)) -lt ${#args[@]} ]]; then
      dep_args[i+1]=/dev/null
    fi
    if [[ "${object_args[i]}" == "$source_file" ]]; then
      object_args[i]="$mini_i"
      saw_input=1
    fi
  done
  test "$saw_c" -eq 1 && test "$saw_input" -eq 1
  # GCC only creates Kbuild dependency metadata; do not use its
  # preprocessed translation unit as the native production input.
  "$real_cc" "${dep_args[@]}" >/dev/null
  "$real_cc" -x cpp-output -fpreprocessed "${object_args[@]}"
  test -n "$output_file" && test -s "$output_file"
  printf 'SUBSTITUTE source=%q output=%q pp=MiniPP compiler=GNU-cc1 exact=1\n' "$source_file" "$output_file" >>"$trace"
  exit 0
fi

exec "$real_cc" "$@"
