#!/usr/bin/env bash
set -Eeuo pipefail

real_cc=${REAL_CC:-riscv64-linux-gnu-gcc}
linux_src=${MINIPP_LINUX_SRC:?MINIPP_LINUX_SRC is not set}
output=${MINIPP_PREDEFINES_OUT:?MINIPP_PREDEFINES_OUT is not set}
# GCC has a distinct builtin-macro environment for .S (assembler-with-cpp).
# The original C pipeline remains the default; do not silently reuse its
# macro profile when validating native preprocessing of real Kbuild .S.
language=${MINIPP_PREDEFINES_LANG:-c}
[[ "$language" == c || "$language" == assembler-with-cpp ]] || {
  echo "MINIPP_REFERENCE_PREDEFINES=FAIL unsupported_language:$language" >&2; exit 2;
}
shift_count=0

mkdir -p "$(dirname "$output")"
tmp="$output.tmp"
rm -f "$tmp"

predef_args=("$@")
# Strict target-aware MiniPP macro producer: derive C PIC/PIE/optimizer
# definitions from the canonical RV64 ABI profile. Existing GNU oracle
# remains separate. Fail closed for any architecture/flag combination that
# has not been classified and tested.
if [[ "${MINIPP_PREDEFINES_NATIVE_RULES:-0}" == 1 ]]; then
  rm -f "$tmp"
  root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
  # Use the SAME native C MiniPP executable for target macro production.
  # Python remains in tests only as the frozen independent differential oracle.
  native_cpp=${MINIPP_NATIVE_CPP:-"$root/build/release/bin/minic-cpp"}
  if [[ ! -x "$native_cpp" ]]; then
    if [[ -n "${MINIPP_NATIVE_CPP:-}" ]]; then
      echo "MINIPP_NATIVE_MACRO_RULES=FAIL missing_native_cpp:$native_cpp" >&2
      exit 2
    fi
    # Build once per worker; subsequent 2064 owner invocations only run C.
    make -s -C "$root" -j4 MODE=release CFLAGS=-Werror \
      build/release/bin/minic-cpp
  fi
  "$native_cpp" --dump-target-predefines \
    --language "$language" --output "$output" -- "${predef_args[@]}"
  exit $?
fi
# Strict opt-in for source-pinned exact RV64 Linux builtin profiles. No GNU
# process executes on this path, and unsupported option sets fail closed.
# The GNU path below remains the independent byte-identity reference oracle.
if [[ -n "${MINIPP_PREDEFINES_SNAPSHOT:-}" ]]; then
  rm -f "$tmp"
  root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
  python3 "$root/tools/ci/minipp-linux-pinned-profile-v1.py" \
    --snapshot "$MINIPP_PREDEFINES_SNAPSHOT" \
    --language "$language" --output "$output" -- "${predef_args[@]}"
  exit $?
fi
"$real_cc" "${predef_args[@]}" -dM -E -x "$language" /dev/null >"$tmp"

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
    # GNU assembler-with-cpp deliberately does not parse __has_attribute()
    # inside #if (it reports "missing binary operator"). The *macro dump*
    # must use assembler dialect for __ASSEMBLER__, but feature queries
    # use the corresponding target C dialect with identical -march/-mabi.
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
