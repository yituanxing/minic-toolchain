#!/usr/bin/env bash
# Opt-in Linux 6.6.143 native tool union from an immutable P0/P1-certified
# 2064-MiniC-object cache; do NOT alter C preprocessing/compilation/assembly.
# SHA-pinned cache must HIT; the seven C producer shards are skipped entirely.
# The first union is MiniAR (GNU fallback only for mPiT) + final-only
# MiniLD + MiniObjcopy; GNU as is kept.
# Actual MiniAS requires newly generated .s input and is a separate next gate.
set -Eeuo pipefail
if (( $# != 2 )); then
  echo "usage: $0 <native-tool-build-dir> <evidence-dir>" >&2
  exit 64
fi
toolchain=$(realpath "$1")
ev=$(realpath -m "$2")
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ev"
make -C "$repo" -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" \
  "$toolchain/bin/minic-ar" "$toolchain/bin/minic-ld" "$toolchain/bin/minic-objcopy"
for pair in "AR minic-ar" "LD minic-ld" "OBJCOPY minic-objcopy"; do
  read -r role name <<<"$pair"
  [[ -x "$toolchain/bin/$name" ]] || {
    echo "NATIVE_UNION=FAIL missing=$name" >&2; exit 10;
  }
  sha256sum "$toolchain/bin/$name" >>"$ev/tools.sha256"
done
export MINIAR="$toolchain/bin/minic-ar"
export MINILD="$toolchain/bin/minic-ld"
export MINIOBJCOPY="$toolchain/bin/minic-objcopy"
export NATIVE_TRACE_DIR="$ev"
cat >"$ev/ar-wrapper" <<'SH_AR'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAR:?}" "${NATIVE_TRACE_DIR:?}"
# Linux vmlinux.a uses GNU ar's positional move (mPiT), with an explicit
# anchor and members. Current MiniAR A1 deliberately rejects that form;
# run only this missing command through GNU while continuing to exercise
# MiniAR for Kbuild's other archive creates/lists. The fallback is visible
# and must not be called a fully native AR integration.
case "${1#-}" in
  mPiT)
    { flock 9; printf 'gnu-ar-fallback-mPiT'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/miniar.trace"
    exec /usr/bin/riscv64-linux-gnu-ar "$@"
    ;;
esac
{ flock 9; printf 'miniar'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/miniar.trace"
exec "$MINIAR" "$@"
SH_AR
cat >"$ev/ld-wrapper" <<'SH_LD'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINILD:?}" "${NATIVE_TRACE_DIR:?}"
# Kbuild Kconfig probes the LD driver with -v/--version BEFORE any actual
# linking. A wrapper must report the GNU-compatible linker dialect, not
# "unknown linker". Routing these discovery probes to the known GNU
# implementation is legitimate: no input objects are linked in a probe.
# Retain MiniLD at only the three explicitly certified final boundaries.
for arg in "$@"; do
  case "$arg" in
    -v|-V|--version)
      exec /usr/bin/riscv64-linux-gnu-ld "$@"
      ;;
  esac
done
args=("$@")
output=
for ((i=0; i<${#args[@]}; ++i)); do
  if [[ "${args[i]}" == -o && $((i+1)) -lt ${#args[@]} ]]; then
    output=${args[i+1]}
    break
  fi
done
# MiniLD has already been integration-tested at EXACTLY these three Linux
# final link boundaries; other GNU linker operations are *not* certified yet.
case "${output##*/}" in
  .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|vmlinux)
    { flock 9; printf 'minild'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace"
    exec "$MINILD" "$@" ;;
  *)
    { flock 9; printf 'gnu-ld'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace"
    exec /usr/bin/riscv64-linux-gnu-ld "$@" ;;
esac
SH_LD
cat >"$ev/objcopy-wrapper" <<'SH_COPY'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIOBJCOPY:?}" "${NATIVE_TRACE_DIR:?}"
{ flock 9; printf 'miniobjcopy'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/miniobjcopy.trace"
exec "$MINIOBJCOPY" "$@"
SH_COPY
chmod +x "$ev/"{ar-wrapper,ld-wrapper,objcopy-wrapper}
: >"$ev/miniar.trace"
: >"$ev/minild.trace"
: >"$ev/miniobjcopy.trace"
printf 'NATIVE_UNION=WRAPPERS_READY ar=%s ld=%s objcopy=%s pp=GNU gcc_c=none as=GNU\n' \
  "$MINIAR" "$MINILD" "$MINIOBJCOPY"
# Scripts called from separate GitHub workflow steps do not inherit exports;
# the parent step must export these stable paths before cohort verification.
