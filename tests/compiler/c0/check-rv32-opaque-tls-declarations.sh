#!/usr/bin/env bash
# GNU target-libc headers contain 'extern __thread int errno;'. MiniC can
# keep a declaration with no use, but cannot accidentally miscompile TLS
# access as an ordinary absolute-global relocation.
set -Eeuo pipefail
: "${MINIC_CC:?}"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '%s\n' 'extern __thread int errno;' \
  'extern _Thread_local int other_errno;' \
  'int plain(void) { return 0; }' >"$dir/decl.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$dir/decl.i" -o "$dir/decl.s"
test -s "$dir/decl.s"
printf '%s\n' 'extern __thread int errno;' \
  'int cannot_read_tls(void) { return errno; }' >"$dir/ref.i"
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$dir/ref.i" -o "$dir/ref.s" 2>"$dir/ref.err"; then
  echo "RV32_TLS_DECL=FAIL silently compiled unsupported TLS memory access" >&2
  exit 1
fi
grep -q 'thread-local object access requires target TLS relocation' "$dir/ref.err"
test ! -s "$dir/ref.s"
printf '%s\n' 'extern __thread int errno;' \
  'int errno;' >"$dir/conflict.i"
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$dir/conflict.i" -o "$dir/conflict.s" 2>"$dir/conflict.err"; then
  echo "RV32_TLS_DECL=FAIL silently merged conflicting TLS declarations" >&2
  exit 1
fi
echo 'RV32_TLS_DECL=PASS target_libc_external_decl=1 tls_access=fail_closed incompatible_redeclaration=reject'
