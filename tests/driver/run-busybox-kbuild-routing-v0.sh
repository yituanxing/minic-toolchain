#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
work=${BUILD_DIR:-$root/build/busybox-kbuild-routing-v0}
rm -rf "$work"
mkdir -p "$work/bin" "$work/sysroot/usr" "$work/uapi" "$work/musl/obj/crt" "$work/musl/lib"
: >"$work/results.tsv"
: >"$work/routes.log"
: >"$work/libgcc.a"
: >"$work/musl/obj/crt/crt1.o"
: >"$work/musl/obj/crt/crti.o"
: >"$work/musl/obj/crt/crtn.o"
: >"$work/musl/lib/libc.a"

cat >"$work/bin/mock-gcc" <<'SH'
#!/usr/bin/env bash
printf 'GNU\t' >>"$ROUTES_LOG"
printf '%q ' "$@" >>"$ROUTES_LOG"
printf '\n' >>"$ROUTES_LOG"
out=
prev=
for arg in "$@"; do
  if [[ "$prev" == -o ]]; then out=$arg; fi
  prev=$arg
done
if [[ -n "$out" && "$out" != /dev/null ]]; then mkdir -p "$(dirname "$out")"; : >"$out"; fi
exit 0
SH
chmod +x "$work/bin/mock-gcc"

cat >"$work/bin/mock-minic" <<'SH'
#!/usr/bin/env bash
printf 'MINI\t' >>"$ROUTES_LOG"
printf '%q ' "$@" >>"$ROUTES_LOG"
printf '\n' >>"$ROUTES_LOG"
out=
prev=
for arg in "$@"; do
  if [[ "$prev" == -o ]]; then out=$arg; fi
  prev=$arg
done
if [[ -n "$out" ]]; then mkdir -p "$(dirname "$out")"; : >"$out"; fi
exit 0
SH
chmod +x "$work/bin/mock-minic"

export ROUTES_LOG="$work/routes.log"
export BUSYBOX_REAL_GCC="$work/bin/mock-gcc"
export BUSYBOX_MINIC="$work/bin/mock-minic"
export BUSYBOX_MINI_SYSROOT="$work/sysroot/usr"
export BUSYBOX_UAPI_COMPAT="$work/uapi"
export BUSYBOX_MUSL_BUILD="$work/musl"
export BUSYBOX_LIBGCC="$work/libgcc.a"
export BUSYBOX_C_RESULTS="$work/results.tsv"
export BUSYBOX_MINIC_TIMEOUT=10s
wrapper="$root/tools/ci/busybox-kbuild-cc-wrapper-v0.sh"

# Identity probe: GNU only.
bash "$wrapper" -dumpmachine
# Kbuild option probe: /dev/null + -xc must stay GNU and must not append libc.a.
bash "$wrapper" -S -xc /dev/null -o "$work/probe.s"
# scripts/trylink style probe: temporary C plus linker option stays GNU.
: >"$work/tmp.trylink.c"
bash "$wrapper" -c "$work/tmp.trylink.c" -Wl,--gc-sections -o "$work/tmp.trylink.o"
# Assembly/relocatable control traffic stays GNU.
: >"$work/input.S"
bash "$wrapper" -c "$work/input.S" -o "$work/input.o"
bash "$wrapper" -r "$work/input.o" -o "$work/combined.o"
# A real target C producer must go through Mini.
: >"$work/real.c"
bash "$wrapper" -Wall -O2 -c "$work/real.c" -o "$work/real.o"

mapfile -t routes <"$work/routes.log"
test "${#routes[@]}" -eq 6
for i in 0 1 2 3 4; do [[ "${routes[$i]}" == GNU$'\t'* ]]; done
[[ "${routes[5]}" == MINI$'\t'* ]]

# Guard against the exact regression that produced the multi-GB log: compiler
# probes must never receive the hosted static-link archive payload.
if sed -n '1,5p' "$work/routes.log" | grep -Fq "$work/musl/lib/libc.a"; then
  echo "probe route incorrectly received libc.a" >&2
  exit 1
fi

test "$(wc -l <"$work/results.tsv")" -eq 1
grep -Fq "$work/real.c" "$work/results.tsv"
grep -Fq $'\tPASS\t0\t' "$work/results.tsv"
echo "BUSYBOX_KBUILD_ROUTING_V0=PASS probes=5 mini_c_producers=1"
