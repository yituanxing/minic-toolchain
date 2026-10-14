#!/usr/bin/env bash
# Runtime qualification replay: Linux vDSO native MiniLD now emits real SHT_SYMTAB
# so Kbuild's plain NM generates __vdso_rt_sigreturn_offset correctly.
# Focused GNU-nm A4 oracle: #38541487379 PASS. Never reuse an empty header.
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
# The exact ELF64 RISC-V Linux vDSO command independently passed GNU-vs-
# MiniLD ELF ABI, section, PHDR and symbol-version oracles (#38535551347).
# Promote only the Kbuild 64-bit vdso.so.dbg link under explicit opt-in.
# Do not capture compat/32-bit vDSO, host links or non-linking LD probes.
# Fail closed if GNU Kbuild changes this target's flags; do not fake zero
# GNU fallback by quietly sending an unrecognized vDSO command to GNU.
if [[ "${MINILD_ENABLE_VDSO64:-0}" == 1 &&
      "$output" == *arch/riscv/kernel/vdso/vdso.so.dbg* ]]; then
  if [[ "$output" != arch/riscv/kernel/vdso/vdso.so.dbg &&
        "$output" != arch/riscv/kernel/vdso/vdso.so.dbg.tmp ]]; then
    echo "MINILD_VDSO64=FAIL unexpected_target_path:$output" >&2
    exit 86
  fi
  has_arch=0
  has_shared=0
  has_script=0
  for ((j=0; j<${#args[@]}; ++j)); do
    case "${args[j]}" in
      -melf64lriscv) has_arch=1 ;;
      -shared) has_shared=1 ;;
      -T)
        if ((j+1<${#args[@]})) &&
           [[ "${args[j+1]}" == *arch/riscv/kernel/vdso/vdso.lds ]]; then
          has_script=1
        fi ;;
      -T*vdso.lds) has_script=1 ;;
    esac
  done
  if (( !has_arch || !has_shared || !has_script )); then
    echo "MINILD_VDSO64=FAIL unexpected_kbuild_flags arch=$has_arch shared=$has_shared script=$has_script output=$output" >&2
    exit 86
  fi
  { flock 9; printf 'minild-vdso64'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
  started_ms=$(date +%s%3N)
  status=0
  "$MINILD" "$@" || status=$?
  elapsed_ms=$(($(date +%s%3N)-started_ms))
  { flock 9; printf 'minild-vdso64\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
  if (( status!=0 )); then
    echo "MINILD_VDSO64=FAIL native_link_status=$status output=$output" >&2
    # Diagnose the REAL Kbuild inputs, not the seven GNU golden inputs of
    # the successful isolated replay. Native MiniC .s / MiniAS .o may
    # legitimately differ in CFI and .eh_frame contribution. We must
    # establish the current input ABI before patching the linker.
    for input in "${args[@]}"; do
      if [[ "$input" != *.o || ! -f "$input" ]]; then
        continue
      fi
      sections=$(riscv64-linux-gnu-readelf -W -S "$input" 2>/dev/null |
                 grep -E '[.]eh_frame([[:space:]]|$)' || true)
      printf 'MINILD_VDSO64_INPUT file=%s sha=%s eh_frame=%s\n' \
        "$input" "$(sha256sum "$input" | cut -d' ' -f1)" "${sections:-NONE}" >&2
      golden="$NATIVE_TRACE_DIR/../gcc/$input"
      if [[ -f "$golden" ]]; then
        golden_sections=$(riscv64-linux-gnu-readelf -W -S "$golden" 2>/dev/null |
                          grep -E '[.]eh_frame([[:space:]]|$)' || true)
        echo "MINILD_VDSO64_GNU_OBJECT file=$input sha=$(sha256sum "$golden" | cut -d' ' -f1) eh_frame=${golden_sections:-NONE}" >&2
      fi
    done
    # GNU's exact same-objects output is diagnostic only: never install
    # this ELF in Kbuild or silently fall back for the user's native build.
    debug_args=("${args[@]}")
    for ((j=0; j<${#debug_args[@]}; ++j)); do
      if [[ "${debug_args[j]}" == -o && $((j+1)) -lt ${#debug_args[@]} ]]; then
        debug_args[j+1]="$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so"
        break
      fi
    done
    if /usr/bin/riscv64-linux-gnu-ld "${debug_args[@]}" \
       >"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stdout" \
       2>"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stderr"; then
      riscv64-linux-gnu-readelf -W -S -l \
        "$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so" |
        grep -E '([.]eh_frame|GNU_EH_FRAME)' |
        sed 's/^/MINILD_VDSO64_GNU_SAME_INPUTS /' >&2 || true
      echo "MINILD_VDSO64_GNU_SAME_INPUTS=PASS diagnostic_only=true GNU_output_not_used=true" >&2
    else
      echo 'MINILD_VDSO64_GNU_SAME_INPUTS=FAIL diagnostic_only=true GNU_output_not_used=true' >&2
      tail -n 12 "$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stderr" >&2
    fi
  fi
  if (( status==0 )); then
    # Linux's generated vdso-offsets.h reads the REGULAR SHT_SYMTAB
    # through NM (not -D/.dynsym). Without the real static table, kernel
    # signal.o cannot compile even if the vDSO is a valid versioned DSO.
    # Fail right here rather than spending another Kbuild pass detecting
    # an empty header. This is the exact next error from #38539948868.
    riscv64-linux-gnu-nm "$output" >"$NATIVE_TRACE_DIR/vdso64-static-nm.txt" || {
      echo 'MINILD_VDSO64=FAIL missing_regular_nm_symtab' >&2
      exit 89
    }
    if ! awk '
        $NF == "__vdso_rt_sigreturn" &&
        $1 ~ /^[[:xdigit:]]+$/ &&
        $2 ~ /^[TtWw]$/ { found = 1 }
        END { exit !found }
      ' "$NATIVE_TRACE_DIR/vdso64-static-nm.txt"; then
      echo 'MINILD_VDSO64=FAIL missing_kernel_rt_sigreturn_offset_source_symbol' >&2
      grep -F '__vdso_' "$NATIVE_TRACE_DIR/vdso64-static-nm.txt" |
        head -n 20 >&2 || true
      exit 89
    fi
    echo 'MINILD_VDSO64_STATIC_SYMTAB=PASS plain_nm=true rt_sigreturn_offset_symbol=true'
    # Compare exactly the current MiniC/MiniAS object inputs to a GNU
    # read-only reference. This catches the no-CFI 0-byte
    # PT_GNU_EH_FRAME case without hiding toolchain provenance.
    debug_args=("${args[@]}")
    for ((j=0; j<${#debug_args[@]}; ++j)); do
      if [[ "${debug_args[j]}" == -o && $((j+1)) -lt ${#debug_args[@]} ]]; then
        debug_args[j+1]="$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so"
        break
      fi
    done
    if ! /usr/bin/riscv64-linux-gnu-ld "${debug_args[@]}" \
       >"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stdout" \
       2>"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stderr"; then
      echo "MINILD_VDSO64=FAIL GNU_same_inputs_reference_failed no_fallback=true" >&2
      exit 87
    fi
    if ! python3 - "$output" "$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so" <<'PY_VDSO_PHDR'
import pathlib,struct,sys
def headers(path):
    data=pathlib.Path(path).read_bytes()
    assert data[:6]==b'\x7fELF\x02\x01'
    phoff=struct.unpack_from('<Q',data,32)[0]
    entsize,count=struct.unpack_from('<HH',data,54)
    values=[]
    for i in range(count):
        o=phoff+i*entsize
        typ,flags,fo,vma,paddr,sz,ms,align=struct.unpack_from('<IIQQQQQQ',data,o)
        values.append((typ,flags,fo,vma,sz,ms))
    return values
native,gnu=map(headers,sys.argv[1:])
assert [x[0] for x in native]==[1,2,4,0x6474e550],native
assert [x[0] for x in gnu]==[1,2,4,0x6474e550],gnu
assert [x[1] for x in native]==[x[1] for x in gnu],(native,gnu)
nh,gh=native[-1],gnu[-1]
assert nh[4:]==gh[4:],(nh,gh)
if gh[4]==0:
    assert nh[2:]==gh[2:]==(0,0,0,0),(nh,gh)
print('MINILD_VDSO64_SAME_INPUTS=PASS native_link=true GNU_reference_diagnostic_only=true eh_header_bytes='+str(nh[4])+' flags='+str(nh[1]))
PY_VDSO_PHDR
    then
      echo "MINILD_VDSO64=FAIL same_input_GNU_program_header_differential" >&2
      exit 88
    fi
  fi
  exit "$status"
fi
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
# CI qualification with the native purgatory final static link must retain
# 2064 SHA-exact MiniC objects and real Linux QEMU P0/P1; successful kernel
# boot alone does NOT exercise kexec/purgatory runtime. Other GNU LD remain.
# The REAL cached GNU purgatory.ro -> purgatory.chk final static link was
# independently proven #38485109077: GNU global exports, ELF64 RV ET_EXEC,
# ABI flags and entry nonzero all match. Promote this ONE boundary only after
# the exact final-runtime opt-in, and require full 2064-MiniC Linux QEMU P1.
if [[ "${MINILD_ENABLE_PURGATORY_STATIC:-0}" == 1 &&
      "${output##*/}" == purgatory.chk ]]; then
  found_entry=0
  found_r=0
  for arg in "$@"; do
    [[ "$arg" == purgatory_start ]] && found_entry=1
    [[ "$arg" == -r || "$arg" == --relocatable ]] && found_r=1
  done
  if [[ "$found_entry" == 1 && "$found_r" == 0 ]]; then
    { flock 9; printf 'minild-purgatory-static'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    start_ms=$(date +%s%3N)
    status=0
    "$MINILD" "$@" || status=$?
    elapsed_ms=$(($(date +%s%3N)-start_ms))
    { flock 9; printf 'minild-purgatory-static\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
    exit "$status"
  fi
fi
# Three static vmlinux links proven #38426379946; 32-bit vDSO GNU LD stays.

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
 \
      "$NATIVE_TRACE_DIR/vdso64-static-nm.txt" || {
      echo 'MINILD_VDSO64=FAIL missing_kernel_rt_sigreturn_offset_source_symbol' >&2
      exit 89
    }
    echo 'MINILD_VDSO64_STATIC_SYMTAB=PASS plain_nm=true rt_sigreturn_offset_symbol=true'
    # Compare exactly the current MiniC/MiniAS object inputs to a GNU
    # read-only reference. This catches the no-CFI 0-byte
    # PT_GNU_EH_FRAME case without hiding toolchain provenance.
    debug_args=("${args[@]}")
    for ((j=0; j<${#debug_args[@]}; ++j)); do
      if [[ "${debug_args[j]}" == -o && $((j+1)) -lt ${#debug_args[@]} ]]; then
        debug_args[j+1]="$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so"
        break
      fi
    done
    if ! /usr/bin/riscv64-linux-gnu-ld "${debug_args[@]}" \
       >"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stdout" \
       2>"$NATIVE_TRACE_DIR/vdso64-gnu-diagnostic.stderr"; then
      echo "MINILD_VDSO64=FAIL GNU_same_inputs_reference_failed no_fallback=true" >&2
      exit 87
    fi
    if ! python3 - "$output" "$NATIVE_TRACE_DIR/vdso64-gnu-same-inputs-diagnostic.so" <<'PY_VDSO_PHDR'
import pathlib,struct,sys
def headers(path):
    data=pathlib.Path(path).read_bytes()
    assert data[:6]==b'\x7fELF\x02\x01'
    phoff=struct.unpack_from('<Q',data,32)[0]
    entsize,count=struct.unpack_from('<HH',data,54)
    values=[]
    for i in range(count):
        o=phoff+i*entsize
        typ,flags,fo,vma,paddr,sz,ms,align=struct.unpack_from('<IIQQQQQQ',data,o)
        values.append((typ,flags,fo,vma,sz,ms))
    return values
native,gnu=map(headers,sys.argv[1:])
assert [x[0] for x in native]==[1,2,4,0x6474e550],native
assert [x[0] for x in gnu]==[1,2,4,0x6474e550],gnu
assert [x[1] for x in native]==[x[1] for x in gnu],(native,gnu)
nh,gh=native[-1],gnu[-1]
assert nh[4:]==gh[4:],(nh,gh)
if gh[4]==0:
    assert nh[2:]==gh[2:]==(0,0,0,0),(nh,gh)
print('MINILD_VDSO64_SAME_INPUTS=PASS native_link=true GNU_reference_diagnostic_only=true eh_header_bytes='+str(nh[4])+' flags='+str(nh[1]))
PY_VDSO_PHDR
    then
      echo "MINILD_VDSO64=FAIL same_input_GNU_program_header_differential" >&2
      exit 88
    fi
  fi
  exit "$status"
fi
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
# CI qualification with the native purgatory final static link must retain
# 2064 SHA-exact MiniC objects and real Linux QEMU P0/P1; successful kernel
# boot alone does NOT exercise kexec/purgatory runtime. Other GNU LD remain.
# The REAL cached GNU purgatory.ro -> purgatory.chk final static link was
# independently proven #38485109077: GNU global exports, ELF64 RV ET_EXEC,
# ABI flags and entry nonzero all match. Promote this ONE boundary only after
# the exact final-runtime opt-in, and require full 2064-MiniC Linux QEMU P1.
if [[ "${MINILD_ENABLE_PURGATORY_STATIC:-0}" == 1 &&
      "${output##*/}" == purgatory.chk ]]; then
  found_entry=0
  found_r=0
  for arg in "$@"; do
    [[ "$arg" == purgatory_start ]] && found_entry=1
    [[ "$arg" == -r || "$arg" == --relocatable ]] && found_r=1
  done
  if [[ "$found_entry" == 1 && "$found_r" == 0 ]]; then
    { flock 9; printf 'minild-purgatory-static'; printf ' %q' "$@"; printf '\n'; } 9>>"$NATIVE_TRACE_DIR/minild.trace" >&9
    start_ms=$(date +%s%3N)
    status=0
    "$MINILD" "$@" || status=$?
    elapsed_ms=$(($(date +%s%3N)-start_ms))
    { flock 9; printf 'minild-purgatory-static\t%s\t%s\t%s\n' "$elapsed_ms" "$status" "$output"; } 9>>"$NATIVE_TRACE_DIR/minild-times.tsv" >&9
    exit "$status"
  fi
fi
# Three static vmlinux links proven #38426379946; 32-bit vDSO GNU LD stays.

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
