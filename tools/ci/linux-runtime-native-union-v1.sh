#!/usr/bin/env bash
# Opt-in Linux 6.6.143 native tool union from an immutable P0/P1-certified
# 2064-MiniC-object cache; do NOT alter C preprocessing/compilation/assembly.
# SHA-pinned cache must HIT; the seven C producer shards are skipped entirely.
# The cached union now exercises MiniAR for EVERY observed Kbuild AR operation
# (including memberless mPiT); MiniLD for three final links; MiniObjcopy.
# GNU as remains pinned.
# Actual MiniAS requires newly generated .s input and is a separate next gate.
# Optimized Linux exact cached GNU-link replay #38426172026 achieved
# MiniLD 1089ms total (layout 771ms) vs baseline 13988ms (layout 13723ms)
# by indexing R_RISCV_ALIGN relocations per section. Focused RV64/AR/LD
# regressions passed #38426050781. Full 2064 MiniC objects still need
# native union P0/P1 re-acceptance with this new MiniLD binary.
# From passing C cache: #38415409019 native two-stage link took 310.798s
# then 306.902s. minild-times.tsv profiles each final-link call separately;
# first establish full Linux P1 correctness before optimizing header preflight.
# Repro: #38413825638 saw malformed 'mPiT' due to ar-list stdout
# corruption. With fd9 logging fixed, real Kbuild mPiT has no explicit
# members; MiniAR correctly handles it natively and GNU AR can be removed.
# Kbuild captures `ar t vmlinux.a` stdout in command substitutions for its
# head-object ordering. NEVER let tool trace output escape to stdout: each
# { flock; printf; } logging group must redirect fd1 to locked fd9.
# The earlier CI showed malformed `ar mPiT miniar t vmlinux.a vmlinux.a`
# precisely because the wrapper's printed trace polluted `ar t` stdout.
# Test gate: stdout stays clean for every wrapper while separate trace
# attests exactly which MiniAR/MiniLD/MiniObjcopy invocations were native.
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
# The *actual* Kbuild invocation is "ar mPiT init/main.o vmlinux.a"
# with NO member list; MiniAR A1 implements this as an exact archive-preserving
# no-op (tests/archiver/run-a1.sh already compares GNU vs MiniAR byte-for-byte).
# The prior fallback was unnecessary and masked a proven native operation.
# Explicit-member mPiT is a separate unsupported operation, fail closed there.
{ flock 9; printf 'miniar'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/miniar.trace" >&9
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
# Combined full-runtime qualification: only vmlinux.o -r is additionally
# native, all other untested linker boundaries stay GNU. Require QEMU P1
# and 2 real native -r calls after SHA-authenticating all 2064 MiniC objects.
# The GNU vmlinux.o relocatable link has independently passed the REAL
# cached Linux 6.6.143 GNU -r ABI and ALL exported-name differential
# (#38481829961): 291ms; zero C objects rebuilt. Promote it only with an
# explicit opt-in; prove QEMU P0/P1 again before calling it accepted.
if [[ "${MINILD_ENABLE_VMLINUX_R:-0}" == 1 && "${output##*/}" == vmlinux.o ]]; then
  found_r=0
  for arg in "$@"; do
    case "$arg" in
      -r|--relocatable) found_r=1 ;;
    esac
  done
  if [[ "$found_r" == 1 ]]; then
    { flock 9; printf 'minild-r'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    start_ms=$(date +%s%3N)
    status=0
    MINILD_PROFILE_PHASES=1 "$MINILD" "$@" || status=$?
    elapsed_ms=$(($(date +%s%3N)-start_ms))
    { flock 9; printf 'minild-r\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
    exit "$status"
  fi
fi
# GNU-pinned 6.6.143 purgatory.ro -r was independently proven in
# #38482832436: 4ms MiniLD, ELF64 RISC-V ET_REL and export names agree.
# Replace exactly this -r boundary under opt-in; leave purgatory.chk,
# vDSO and all other unproven GNU LD operations untouched.
if [[ "${MINILD_ENABLE_PURGATORY_R:-0}" == 1 &&
      "${output##*/}" == purgatory.ro ]]; then
  found_r=0
  found_entry=0
  for arg in "$@"; do
    [[ "$arg" == -r || "$arg" == --relocatable ]] && found_r=1
    [[ "$arg" == purgatory_start ]] && found_entry=1
  done
  if [[ "$found_r" == 1 && "$found_entry" == 1 ]]; then
    { flock 9; printf 'minild-purgatory-r'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    start_ms=$(date +%s%3N)
    status=0
    "$MINILD" "$@" || status=$?
    elapsed_ms=$(($(date +%s%3N)-start_ms))
    { flock 9; printf 'minild-purgatory-r\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
    exit "$status"
  fi
fi
# The three static final Linux link boundaries are proven #38426379946.
# Purgatory and 32-bit vDSO continue using GNU LD until individually tested.
case "${output##*/}" in
  .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|vmlinux)
    { flock 9; printf 'minild'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    start_ms=$(date +%s%3N)
    status=0
    MINILD_PROFILE_PHASES=1 "$MINILD" "$@" || status=$?
    elapsed_ms=$(($(date +%s%3N) - start_ms))
    { flock 9; printf 'minild\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
    exit "$status" ;;
  *)
    { flock 9; printf 'gnu-ld'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    exec /usr/bin/riscv64-linux-gnu-ld "$@" ;;
esac
SH_LD
cat >"$ev/objcopy-wrapper" <<'SH_COPY'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIOBJCOPY:?}" "${NATIVE_TRACE_DIR:?}"
{ flock 9; printf 'miniobjcopy'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/miniobjcopy.trace" >&9
exec "$MINIOBJCOPY" "$@"
SH_COPY
chmod +x "$ev/"{ar-wrapper,ld-wrapper,objcopy-wrapper}
: >"$ev/miniar.trace"
: >"$ev/minild.trace"
: >"$ev/minild-times.tsv"
: >"$ev/miniobjcopy.trace"
printf 'NATIVE_UNION=WRAPPERS_READY ar=%s ld=%s objcopy=%s pp=GNU gcc_c=none as=GNU\n' \
  "$MINIAR" "$MINILD" "$MINIOBJCOPY"
# Scripts called from separate GitHub workflow steps do not inherit exports;
# the parent step must export these stable paths before cohort verification.
