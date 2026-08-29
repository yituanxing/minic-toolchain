#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
linux_src=${MINIPP_LINUX_SRC:?MINIPP_LINUX_SRC is not set}
output=${MINIPP_PREDEFINES_OUT:?MINIPP_PREDEFINES_OUT is not set}
shift_count=0

mkdir -p "$(dirname "$output")"
tmp="$output.tmp"
rm -f "$tmp"

predef_args=("$@")
"$real_cc" "${predef_args[@]}" -dM -E -x c /dev/null >"$tmp"

cat >>"$tmp" <<'EOF'
#define __MINIPP_PP_CAT2(a, b) a##b
#define __MINIPP_PP_CAT(a, b) __MINIPP_PP_CAT2(a, b)
EOF

for capability in attribute builtin; do
  names="$output.$capability.names"
  grep -RhoE "__has_${capability}\([[:space:]]*[A-Za-z_][A-Za-z0-9_]*"     "$linux_src/include" "$linux_src/arch/riscv/include" 2>/dev/null |
    sed -E "s/^__has_${capability}\([[:space:]]*//" |
    LC_ALL=C sort -u >"$names" || true

  while IFS= read -r name; do
    [[ -n "$name" ]] || continue
    value=$(
      printf '#if __has_%s(%s)\n1\n#else\n0\n#endif\n'         "$capability" "$name" |
        "$real_cc" "${predef_args[@]}" -E -P -x c - |
        tail -n 1
    )
    upper=${capability^^}
    printf '#define __MINIPP_HAS_%s_%s %s\n'       "$upper" "$name" "$value" >>"$tmp"
  done <"$names"

  upper=${capability^^}
  printf '#define __has_%s(x) __MINIPP_PP_CAT(__MINIPP_HAS_%s_, x)\n'     "$capability" "$upper" >>"$tmp"
done

mv "$tmp" "$output"

grep -F '#define __has_attribute(x)' "$output" >/dev/null
grep -F '#define __has_builtin(x)' "$output" >/dev/null
printf 'MINIPP_REFERENCE_PREDEFINES=PASS macros=%s attributes=%s builtins=%s\n'   "$(wc -l <"$output")"   "$(wc -l <"$output.attribute.names")"   "$(wc -l <"$output.builtin.names")"
