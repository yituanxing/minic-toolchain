#!/bin/sh
# GNU glibc __SOCKADDR_ARG-style callback bridge, actual target GNU ABI.
set -eu
test "$#" -eq 4
: "${MINIC_CC:?set MINIC_CC}"
target=$1
gcc=$2
qemu=$3
sysroot=$4
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$repo/tests/compiler/c0/gnu_transparent_union_callback.c"
"$gcc" -E -P -std=gnu11 "$source" -o "$work/bridge.i"
if ! timeout 30s "$MINIC_CC" --target="$target-linux" -S \
      "$work/bridge.i" -o "$work/bridge.s" >"$work/mini.out" 2>"$work/mini.err"; then
  echo "FOUR_BACKEND_TRANSPARENT_UNION=FAIL target=$target stage=MiniC" >&2
  tail -n 25 "$work/mini.err" >&2 || true
  exit 1
fi
if ! "$gcc" -std=gnu11 -O0 -Werror=incompatible-pointer-types \
     "$source" -o "$work/ref.elf" >"$work/ref.build" 2>&1 ||
   ! "$gcc" "$work/bridge.s" -o "$work/mini.elf" \
      >"$work/mini.build" 2>&1; then
  echo "FOUR_BACKEND_TRANSPARENT_UNION=FAIL target=$target stage=GNU_AS_LINK" >&2
  tail -n 20 "$work/ref.build" "$work/mini.build" >&2 || true
  exit 1
fi
for which in ref mini; do
  if ! timeout 20s "$qemu" -L "$sysroot" "$work/$which.elf" \
      >"$work/$which.out" 2>"$work/$which.err"; then
    echo "FOUR_BACKEND_TRANSPARENT_UNION=FAIL target=$target stage=QEMU_$which" >&2
    cat "$work/$which.err" >&2 || true
    exit 1
  fi
done
cmp "$work/ref.out" "$work/mini.out"
cmp "$work/ref.err" "$work/mini.err"
grep -qx 'transparent-union-callback: 34' "$work/mini.out"
"$gcc" -E -P -std=gnu11 \
    "$repo/tests/compiler/c0/invalid_gnu_transparent_union_callback.c" \
    -o "$work/invalid.i"
if "$MINIC_CC" --target="$target-linux" -S \
      "$work/invalid.i" -o "$work/invalid.s" \
      >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo "FOUR_BACKEND_TRANSPARENT_UNION=FAIL target=$target stage=unrelated_signature_accepted" >&2
  exit 1
fi
grep -q 'call argument type does not match declaration' "$work/invalid.err"
echo "FOUR_BACKEND_TRANSPARENT_UNION=PASS target=$target GNU_QEMU_byte_exact=1 incompatible_callback_rejected=1"
