#!/bin/sh
# Native x86-64 GNU vs MiniC differential for the existing vendored AES-128 ECB
# upstream acceptance harness. Reuses the exact sources used by RV64 probe.sh.
set -eu

: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "SKIP x86_64 tiny-AES: native runner is not x86-64"; exit 0 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
vendor="$root/tests/vendor/tiny-aes-c/upstream"
fixture="$root/tests/external/tiny-aes-c/aes128_ecb_vectors.c"
headers="$root/tests/external/tiny-aes-c/include"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Uphold the existing frozen-vendor identity contract from PROVENANCE.
test "$(git -C "$root" hash-object "$vendor/aes.c")" = 4481f7b24ec964019d38669842913fd571d28ba3
test "$(git -C "$root" hash-object "$vendor/aes.h")" = b29b6683549632676ec11c06eb86efd02964db57

"$HOST_CC" -std=c11 -O0 -no-pie \
  -I"$vendor" -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
  "$fixture" -o "$work/gcc.bin"

# Exactly as for the existing RV64 acceptance gate: use frozen local headers,
# not the host's system-header expansion.
"$HOST_CC" -E -P -nostdinc -x c -I"$headers" -I"$vendor" \
  -DECB=1 -DCBC=0 -DCTR=0 -DMULTIPLY_AS_A_FUNCTION=1 \
  "$fixture" -o "$work/aes.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/aes.i" -o "$work/minic.s"
"$HOST_CC" -no-pie -x assembler "$work/minic.s" -o "$work/minic.bin"

run_capture() {
  name="$1"
  set +e
  "$work/$name.bin" >"$work/$name.stdout" 2>"$work/$name.stderr"
  code="$?"
  set -e
  printf '%s\n' "$code" >"$work/$name.status"
}

run_capture gcc
run_capture minic

if ! cmp -s "$work/gcc.status" "$work/minic.status" ||
   ! cmp -s "$work/gcc.stdout" "$work/minic.stdout" ||
   ! cmp -s "$work/gcc.stderr" "$work/minic.stderr"; then
  echo "FAIL x86_64 tiny-AES: MiniC/GCC observable outputs differ" >&2
  printf 'GCC exit=%s MiniC exit=%s\n' "$(cat "$work/gcc.status")" "$(cat "$work/minic.status")" >&2
  diff -u "$work/gcc.stdout" "$work/minic.stdout" >&2 || true
  diff -u "$work/gcc.stderr" "$work/minic.stderr" >&2 || true
  exit 1
fi
test "$(cat "$work/minic.status")" = 0
test -s "$work/minic.s"
echo "PASS x86_64 tiny-AES-128-ECB GCC/MiniC differential: exit=0 stdout/stderr equal"
