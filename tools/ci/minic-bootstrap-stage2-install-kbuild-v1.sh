#!/usr/bin/env bash
# Temporarily use SHA-attested Stage2 RV64 CC in the EXISTING seven-runner
# GNU-Kbuild driver. Original Stage0 is retained for independent verification.
# Usage: ... <existing-host-toolchain-bin> <downloaded-stage2>
set -Eeuo pipefail
[[ $# == 2 ]] || { echo "BOOTSTRAP_STAGE2_INSTALL=FAIL arguments" >&2; exit 64; }
bin=$(realpath "$1")
guest=$(realpath "$2")
test -x "$bin/minic" && test -x "$bin/minic-cc" && test -s "$guest"
sha2=17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
sha0=f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd
printf '%s  %s\n' "$sha2" "$guest" | sha256sum -c -
printf '%s  %s\n' "$sha0" "$bin/minic-cc" | sha256sum -c -
test ! -e "$bin/minic.stage0"
test ! -e "$bin/minic-cc.stage0"
test ! -e "$bin/minic-cc.stage2.rv64"
command -v qemu-riscv64 >/dev/null
mv "$bin/minic" "$bin/minic.stage0"
mv "$bin/minic-cc" "$bin/minic-cc.stage0"
cp "$guest" "$bin/minic-cc.stage2.rv64"
printf '%s  %s\n' "$sha2" "$bin/minic-cc.stage2.rv64" | sha256sum -c -
cat > "$bin/minic-cc" <<'SH'
#!/bin/sh
# Certified Stage2 RV64 SHA256:
# 17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]; then
  input=
  output=
  prior=
  for arg do
    case "$prior" in
      -S) input=$arg ;;
      -o) output=$arg ;;
    esac
    prior=$arg
  done
  printf '%s\t%s\n' "$input" "$output" >> "$COHORT_STAGE2_INVOKE_TRACE"
fi
exec qemu-riscv64 "$dir/minic-cc.stage2.rv64" "$@"
SH
# Cache identity hashes bin/minic, NOT only bin/minic-cc. An exact SHA-
# marked deterministic wrapper guarantees Stage2 & Stage0 cannot share a key.
cat > "$bin/minic" <<'SH'
#!/bin/sh
# Compiles with certified self-hosted Stage2 RV64 SHA256:
# 17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$dir/minic.stage0" "$@"
SH
chmod 755 "$bin/minic" "$bin/minic-cc"
test -x "$bin/minic.stage0" && test -x "$bin/minic-cc.stage0"
test "$(sha256sum "$bin/minic-cc.stage2.rv64" | cut -d' ' -f1)" = "$sha2"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'int value(void){return 42;}\n' > "$work/in.c"
riscv64-linux-gnu-gcc -E -P "$work/in.c" -o "$work/in.i"
"$bin/minic" -S "$work/in.i" -o "$work/out.s"
test -s "$work/out.s"
echo "BOOTSTRAP_STAGE2_INSTALL=PASS stage0_cc=$sha0 stage2_guest=$sha2 driver_sha=$(sha256sum "$bin/minic" | cut -d' ' -f1) no_host_compiler_fallback=true smoke_driver_to_qemu=true"
