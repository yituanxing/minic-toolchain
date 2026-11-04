#!/usr/bin/env bash
# Opt-in, disposable Linux 6.6.143 RV64 full-link experiment.
# Input: SHA-certified, QEMU-proven GNU Kbuild pool. Never compile kernel C.
# Linux's original scripts/link-vmlinux.sh owns kallsyms and the final link;
# its three external command boundaries are guarded below.
set -Eeuo pipefail
export LC_ALL=C

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
helper="$repo/tools/ci/minild-rv64-linux-cached-full-image-experiment-v1.sh"
golden_config=eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe
linux_archive_sha=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932

die() { echo "MINILD_RV64_EXPERIMENT=FAIL $*" >&2; exit 1; }
digest() { sha256sum "$1" | cut -d' ' -f1; }
property() { sed -n "s/^$1=//p" "$2"; }
must_file() { [[ -s $1 ]] || die "missing_input=$1"; }
must_exec() { [[ -x $1 ]] || die "missing_executable=$1"; }
cfg_enabled() { grep -Fxq "$1=y" "$MINILD_EXP_OUT/include/config/auto.conf"; }

need_guard_env() {
  : "${MINILD_EXP_OUT:?missing MINILD_EXP_OUT}"
  : "${MINILD_EXP_EV:?missing MINILD_EXP_EV}"
  : "${MINILD_EXP_SRC:?missing MINILD_EXP_SRC}"
  : "${MINILD_EXP_MINI:?missing MINILD_EXP_MINI}"
  : "${MINILD_EXP_GNU_CC:?missing MINILD_EXP_GNU_CC}"
  [[ $PWD == "$MINILD_EXP_OUT" ]] || die "guard_cwd_mismatch=$PWD"
}

# link-vmlinux.sh unconditionally calls Make for init/version-timestamp.o.
# Its exact GNU object is already frozen and SHA-checked. Reject any other
# recursive Make call, not merely commands that happen to compile C.
guard_make() {
  need_guard_env
  [[ $# == 4 && $1 == -f && $2 == "$MINILD_EXP_SRC/scripts/Makefile.build" &&
     $3 == obj=init && $4 == init/version-timestamp.o ]] ||
    die "unexpected_recursive_make=$*"
  (cd "$MINILD_EXP_OUT" && sha256sum -q -c "$MINILD_EXP_EV/version-timestamp.sha256") ||
    die 'frozen_version_timestamp_changed'
  printf '%s\n' 'frozen_existing_init/version-timestamp.o' >>"$MINILD_EXP_EV/make-guard-invocations.log"
  echo 'MINILD_RV64_FROZEN_MAKE=PASS regenerated_kernel_C_objects=0'
}

# This is the ONLY allowed target compiler invocation: GNU as via gcc on the
# newly generated kallsyms assembly, never any kernel .c translation unit.
guard_cc() {
  need_guard_env
  local want_output=false output='' source='' arg
  local have_compile=false
  for arg in "$@"; do
    if $want_output; then output=$arg; want_output=false; continue; fi
    case "$arg" in
      -c) have_compile=true ;;
      -o) want_output=true ;;
      *.S) [[ -z $source ]] || die 'multiple_asm_sources'; source=$arg ;;
      *.c|*.i|*.cc|*.cpp|*.C) die "kernel_C_compile_forbidden=$arg" ;;
    esac
  done
  $have_compile || die 'assembler_missing_compile_flag'
  case "$output:$source" in
    .tmp_vmlinux.kallsyms1.o:.tmp_vmlinux.kallsyms1.S|\
    .tmp_vmlinux.kallsyms2.o:.tmp_vmlinux.kallsyms2.S|\
    .tmp_vmlinux.kallsyms3.o:.tmp_vmlinux.kallsyms3.S) ;;
    *) die "unexpected_GNU_AS_output=$output source=$source" ;;
  esac
  must_file "$MINILD_EXP_OUT/$source"
  local label=${output%.o}
  printf '%s\0' "$@" >"$MINILD_EXP_EV/${label}.gcc-argv0"
  "$MINILD_EXP_GNU_CC" "$@" \
    >"$MINILD_EXP_EV/${label}.gcc.stdout" \
    2>"$MINILD_EXP_EV/${label}.gcc.stderr" || die "GNU_AS_failed=$output"
  must_file "$MINILD_EXP_OUT/$output"
  sha256sum "$MINILD_EXP_OUT/$source" "$MINILD_EXP_OUT/$output" \
    >"$MINILD_EXP_EV/${label}.generated-assembly.sha256"
  printf '%s\n' "$output" >>"$MINILD_EXP_EV/gnu-as-guard-invocations.log"
  echo "MINILD_RV64_GNU_AS=PASS generated=$output source=MiniLD_kallsyms"
}

# A strict LD adapter proves *each* authentic Kbuild output used MiniLD.
# No GNU ld fallback is permitted in this experimental final-link boundary.
guard_ld() {
  need_guard_env
  local want_output=false output='' arg have_script=false have_archive=false
  for arg in "$@"; do
    if $want_output; then output=$arg; want_output=false; continue; fi
    case "$arg" in
      -o|--output) want_output=true ;;
      --output=*) output=${arg#*=} ;;
      -o?*) output=${arg#-o} ;;
      --script=./arch/riscv/kernel/vmlinux.lds) have_script=true ;;
      vmlinux.a) have_archive=true ;;
      -r|--relocatable) die 'relocatable_GNU_link_forbidden' ;;
    esac
  done
  case "$output" in
    .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|\
    .tmp_vmlinux.kallsyms3|vmlinux) ;;
    *) die "unexpected_ld_output=$output" ;;
  esac
  $have_script && $have_archive || die 'not_a_real_kbuild_kernel_link'
  printf '%s\0' "$@" >"$MINILD_EXP_EV/$output.minild-argv0"
  local start elapsed status=0
  start=$(date +%s%3N)
  MINILD_PROFILE_PHASES=1 timeout 240s "$MINILD_EXP_MINI" "$@" \
    >"$MINILD_EXP_EV/$output.minild.stdout" \
    2>"$MINILD_EXP_EV/$output.minild.stderr" || status=$?
  elapsed=$(( $(date +%s%3N) - start ))
  printf '%s\t%s\n' "$elapsed" "$status" >"$MINILD_EXP_EV/$output.minild-timing.tsv"
  if [[ $status != 0 || ! -s $MINILD_EXP_OUT/$output ]]; then
    tail -n 60 "$MINILD_EXP_EV/$output.minild.stderr" >&2 || true
    die "MiniLD_link_failed=$output status=$status"
  fi
  sha256sum "$MINILD_EXP_OUT/$output" >"$MINILD_EXP_EV/$output.minild.sha256"
  printf '%s\n' "$output" >>"$MINILD_EXP_EV/link-guard-invocations.log"
  echo "MINILD_RV64_LINK=PASS output=$output linker=MiniLD elapsed_ms=$elapsed"
}

# The provenance is the producer's *actual* successful GNU, QEMU-certified
# Linux configuration, not a standalone target compiled with another config.
verify_golden() {
  [[ $# == 3 ]] || die 'usage=verify-golden OUT PROVENANCE EVIDENCE'
  local out meta_dir ev meta manifest count name
  out=$(realpath "$1"); meta_dir=$(realpath "$2")
  ev=$(realpath -m "$3"); mkdir -p "$ev"
  meta="$meta_dir/gcc-baseline.txt"
  manifest="$meta_dir/gcc-objects.sha256"
  for name in "$meta" "$manifest" "$out/.config" "$out/include/config/auto.conf" \
    "$out/vmlinux" "$out/arch/riscv/boot/Image" "$out/vmlinux.a" \
    "$out/.vmlinux.export.o" "$out/init/version-timestamp.o" \
    "$out/drivers/firmware/efi/libstub/lib.a" \
    "$out/arch/riscv/kernel/vmlinux.lds"; do must_file "$name"; done
  [[ $(property schema "$meta") == linux-gnu-exact-config-v1 ]] || die 'bad_provenance_schema'
  [[ $(property linux_release "$meta") == 6.6.143 ]] || die 'bad_linux_release'
  [[ $(property linux_archive_sha256 "$meta") == "$linux_archive_sha" ]] || die 'bad_linux_archive'
  [[ $(property qemu_contract "$meta") == full_and_p1_passed ]] || die 'uncertified_GNU_pool'
  [[ $(property config_sha256 "$meta") == "$golden_config" ]] || die 'wrong_historical_config'
  [[ $(digest "$out/.config") == "$golden_config" ]] || die 'golden_config_sha_mismatch'
  [[ $(property vmlinux_sha256 "$meta") == "$(digest "$out/vmlinux")" ]] || die 'GNU_vmlinux_sha_mismatch'
  [[ $(property image_sha256 "$meta") == "$(digest "$out/arch/riscv/boot/Image")" ]] || die 'GNU_Image_sha_mismatch'
  for name in gcc ld ar; do
    local binary="riscv64-linux-gnu-$name" absolute property_name
    absolute=$(command -v "$binary") || die "missing_GNU_tool=$binary"
    property_name="gnu_${name}_binary_sha256"
    [[ $name != gcc ]] || property_name=gcc_binary_sha256
    [[ "$(digest "$absolute")" == "$(property "$property_name" "$meta")" ]] ||
      die "GNU_tool_identity_mismatch=$name"
    sha256sum "$absolute" >>"$ev/gnu-tool-sha256.txt"
  done
  [[ $(property gcc_version "$meta") == "$(riscv64-linux-gnu-gcc -dumpfullversion)" ]] ||
    die 'GNU_gcc_version_mismatch'
  [[ $(property gcc_target "$meta") == "$(riscv64-linux-gnu-gcc -dumpmachine)" ]] ||
    die 'GNU_gcc_target_mismatch'
  count=$(wc -l <"$manifest")
  [[ "$count" =~ ^[0-9]+$ && $count -ge 1500 &&
     $(property gcc_object_count "$meta") == "$count" ]] || die 'GNU_object_count_mismatch'
  python3 - "$manifest" <<'PY_MANIFEST'
from pathlib import Path
import re
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
seen = set()
for line in lines:
    m = re.fullmatch(r'([0-9a-f]{64})  (.+\.o)', line)
    if m is None:
        raise SystemExit('MINILD_RV64_GOLDEN=FAIL malformed_manifest')
    p = Path(m.group(2))
    if p.is_absolute() or '..' in p.parts or str(p) in seen:
        raise SystemExit('MINILD_RV64_GOLDEN=FAIL unsafe_or_duplicate_manifest')
    seen.add(str(p))
PY_MANIFEST
  (cd "$out" && sha256sum -q -c "$manifest") || die 'GNU_object_manifest_sha_mismatch'
  cp "$meta" "$ev/verified-gcc-baseline.txt"
  cp "$manifest" "$ev/verified-original-gnu-objects.sha256"
  # Only generated kallsyms .o are intentionally mutable in the full link.
  # All true Linux input .o (including init/version-timestamp.o) stay frozen.
  sed '/  \.tmp_vmlinux\.kallsyms[123]\.o$/d' "$manifest" \
    >"$ev/immutable-gnu-objects.sha256"
  (cd "$out" && sha256sum -q -c "$ev/immutable-gnu-objects.sha256") ||
    die 'immutable_GNU_object_manifest_failed'
  (cd "$out" && sha256sum init/version-timestamp.o) >"$ev/version-timestamp.sha256"
  sha256sum "$out/vmlinux.a" "$out/.vmlinux.export.o" \
    "$out/drivers/firmware/efi/libstub/lib.a" \
    "$out/arch/riscv/kernel/vmlinux.lds" >"$ev/gnu-frozen-extra-inputs.sha256"
  # Original GNU vmlinux/Image must pass provenance before Stage A but will
  # intentionally be replaced (vmlinux) or bypassed (Image.minild) thereafter.
  sha256sum "$out/vmlinux" "$out/arch/riscv/boot/Image" \
    >"$ev/original-gnu-kernel-outputs.sha256"
  for name in nm objcopy readelf; do
    local binary="riscv64-linux-gnu-$name"
    command -v "$binary" >/dev/null || die "missing_GNU_tool=$binary"
    sha256sum "$(command -v "$binary")" >>"$ev/gnu-tool-sha256.txt"
  done
  echo "MINILD_RV64_GOLDEN=PASS objects=$count sha_verified=true config=$golden_config QEMU_proven=true"
}

setup_guard_env() {
  local out=$1 src=$2 ev=$3 mini=$4
  export MINILD_EXP_OUT="$out" MINILD_EXP_SRC="$src" MINILD_EXP_EV="$ev"
  export MINILD_EXP_MINI="$mini"
  export MINILD_EXP_GNU_CC
  MINILD_EXP_GNU_CC=$(command -v riscv64-linux-gnu-gcc) || die 'missing_GNU_CC'
}

verify_source() {
  local src=$1 archive=$2
  must_file "$archive"
  [[ $(digest "$archive") == "$linux_archive_sha" ]] || die 'source_archive_sha_mismatch'
  must_file "$src/scripts/link-vmlinux.sh"
  must_file "$src/scripts/mksysmap"
  must_file "$src/scripts/file-size.sh"
  grep -Fxq 'VERSION = 6' "$src/Makefile" || die 'unexpected_kernel_version'
  grep -Fxq 'PATCHLEVEL = 6' "$src/Makefile" || die 'unexpected_kernel_patchlevel'
  grep -Fxq 'SUBLEVEL = 143' "$src/Makefile" || die 'unexpected_kernel_sublevel'
}

verify_frozen_after() {
  local out=$1 ev=$2
  (cd "$out" && sha256sum -q -c "$ev/immutable-gnu-objects.sha256") ||
    die 'frozen_GNU_objects_mutated'
  (cd "$out" && sha256sum -q -c "$ev/version-timestamp.sha256") ||
    die 'frozen_version_timestamp_mutated'
  (cd "$out" && sha256sum -q -c "$ev/gnu-frozen-extra-inputs.sha256") ||
    die 'frozen_link_inputs_mutated'
}

full_link() {
  [[ $# == 5 ]] || die 'usage=full-link OUT SRC ARCHIVE EVIDENCE MINILD'
  local out src archive ev mini asm_march kbuild_asm_flags kbuild_includes flags cppflags
  out=$(realpath "$1"); src=$(realpath "$2"); archive=$(realpath "$3")
  ev=$(realpath "$4"); mini=$(realpath "$5")
  must_exec "$mini"
  must_file "$ev/verified-original-gnu-objects.sha256"
  must_file "$ev/immutable-gnu-objects.sha256"
  verify_source "$src" "$archive"
  setup_guard_env "$out" "$src" "$ev" "$mini"
  cd "$out"
  [[ ! -e $ev/stage-a.ok ]] || die 'stage_A_already_run_in_this_job'
  for flags in CONFIG_ARCH_RV64I CONFIG_KALLSYMS CONFIG_MODULES CONFIG_EFI_STUB; do
    cfg_enabled "$flags" || die "unexpected_kernel_config_disabled=$flags"
  done
  for flags in CONFIG_RELOCATABLE CONFIG_DEBUG_INFO_BTF CONFIG_LTO_CLANG \
    CONFIG_DYNAMIC_FTRACE CONFIG_LD_DEAD_CODE_DATA_ELIMINATION; do
    if cfg_enabled "$flags"; then die "unsupported_pinned_kernel_config=$flags"; fi
  done
  must_exec "$out/scripts/kallsyms"
  if cfg_enabled CONFIG_BUILDTIME_TABLE_SORT; then must_exec "$out/scripts/sorttable"; fi
  verify_frozen_after "$out" "$ev"
  sha256sum "$mini" >"$ev/minild-tool.sha256"
  asm_march=rv64ima
  if cfg_enabled CONFIG_FPU; then asm_march+=fd; fi
  if cfg_enabled CONFIG_RISCV_ISA_C; then asm_march+=c; fi
  if cfg_enabled CONFIG_RISCV_ISA_V; then asm_march+=v; fi
  kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=lp64 -march=$asm_march"
  if cfg_enabled CONFIG_TOOLCHAIN_NEEDS_OLD_ISA_SPEC; then
    kbuild_asm_flags+=' -Wa,-misa-spec=2.2'
  elif cfg_enabled CONFIG_TOOLCHAIN_NEEDS_EXPLICIT_ZICSR_ZIFENCEI; then
    asm_march+=_zicsr_zifencei
    kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=lp64 -march=$asm_march"
  fi
  if cfg_enabled CONFIG_TOOLCHAIN_HAS_ZIHINTPAUSE; then
    asm_march+=_zihintpause
    kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=lp64 -march=$asm_march"
    if cfg_enabled CONFIG_TOOLCHAIN_NEEDS_OLD_ISA_SPEC; then
      kbuild_asm_flags+=' -Wa,-misa-spec=2.2'
    fi
  fi
  kbuild_asm_flags+=' -mno-riscv-attribute -Wa,-mno-arch-attr'
  kbuild_includes="-I$src/arch/riscv/include -I$out/arch/riscv/include/generated"
  kbuild_includes+=" -I$src/include -I$out/include"
  kbuild_includes+=" -I$src/arch/riscv/include/uapi -I$out/arch/riscv/include/generated/uapi"
  kbuild_includes+=" -I$src/include/uapi -I$out/include/generated/uapi"
  kbuild_includes+=" -include $src/include/linux/compiler-version.h"
  kbuild_includes+=" -include $src/include/linux/kconfig.h"
  cppflags="-D__KERNEL__ -fmacro-prefix-map=$src/="
  export srctree="$src" objtree=. SRCARCH=riscv CONFIG_SHELL=sh
  export KBUILD_LDS=arch/riscv/kernel/vmlinux.lds
  export KBUILD_VMLINUX_LIBS=./drivers/firmware/efi/libstub/lib.a
  export NM=riscv64-linux-gnu-nm OBJCOPY=riscv64-linux-gnu-objcopy
  export NOSTDINC_FLAGS=-nostdinc
  export LINUXINCLUDE="$kbuild_includes" KBUILD_CPPFLAGS="$cppflags"
  export KBUILD_AFLAGS="$kbuild_asm_flags" KBUILD_AFLAGS_KERNEL=''
  export KBUILD_VERBOSE=1
  export MAKE="bash $helper guard-make"
  export CC="bash $helper guard-cc"
  export LD="bash $helper guard-ld"
  # Recorded authentic GNU kallsyms1 argv (CI #41856691245).
  local kbuild_ldflags='-melf64lriscv -z noexecstack --no-warn-rwx-segments'
  local vmlinux_ldflags='-z norelro --build-id=sha1 --orphan-handling=warn'
  printf 'source=%s\ncache_config=%s\nGNU_CC=%s\nMiniLD=%s\nKBUILD_AFLAGS=%s\nLINUXINCLUDE=%s\n' \
    "$src" "$golden_config" "$MINILD_EXP_GNU_CC" "$mini" "$kbuild_asm_flags" "$kbuild_includes" \
    >"$ev/phase-a-exact-kbuild-env.txt"
  local rc=0
  sh "$src/scripts/link-vmlinux.sh" "$LD" "$kbuild_ldflags" "$vmlinux_ldflags" \
    >"$ev/link-vmlinux.stdout" 2>"$ev/link-vmlinux.stderr" || rc=$?
  if [[ $rc != 0 ]]; then
    tail -n 90 "$ev/link-vmlinux.stderr" >&2 || true
    die "original_Kbuild_link_vmlinux_failed=rc_$rc"
  fi
  for flags in .tmp_vmlinux.kallsyms1 .tmp_vmlinux.kallsyms2 vmlinux; do
    must_file "$out/$flags"
    must_file "$ev/$flags.minild-argv0"
  done
  for flags in 1 2; do
    must_file "$ev/.tmp_vmlinux.kallsyms$flags.gcc-argv0"
    must_file "$ev/.tmp_vmlinux.kallsyms$flags.generated-assembly.sha256"
  done
  must_file "$ev/link-guard-invocations.log"
  must_file "$ev/gnu-as-guard-invocations.log"
  must_file "$ev/make-guard-invocations.log"
  [[ $(wc -l <"$ev/make-guard-invocations.log") == 1 ]] ||
    die 'unexpected_recursive_Make_invocation_count'
  local ld_order as_order
  ld_order=$(paste -sd, "$ev/link-guard-invocations.log")
  as_order=$(paste -sd, "$ev/gnu-as-guard-invocations.log")
  case "$ld_order:$as_order" in
    '.tmp_vmlinux.kallsyms1,.tmp_vmlinux.kallsyms2,vmlinux:.tmp_vmlinux.kallsyms1.o,.tmp_vmlinux.kallsyms2.o'|\
    '.tmp_vmlinux.kallsyms1,.tmp_vmlinux.kallsyms2,.tmp_vmlinux.kallsyms3,vmlinux:.tmp_vmlinux.kallsyms1.o,.tmp_vmlinux.kallsyms2.o,.tmp_vmlinux.kallsyms3.o') ;;
    *) die "unexpected_link_or_assembly_sequence=ld_{$ld_order}_as_{$as_order}" ;;
  esac
  must_file "$out/System.map"
  must_file "$out/.vmlinux.d"
  verify_frozen_after "$out" "$ev"
  riscv64-linux-gnu-readelf -W -h "$out/vmlinux" >"$ev/final-vmlinux-elf-header.txt"
  grep -Eq 'Machine:.*RISC-V' "$ev/final-vmlinux-elf-header.txt" || die 'not_RISCV_ELF'
  riscv64-linux-gnu-readelf -W -S "$out/vmlinux" >"$ev/final-vmlinux-sections.txt"
  riscv64-linux-gnu-nm "$out/vmlinux" >"$ev/final-vmlinux-symbols.txt"
  grep -Eq ' [Tt] (_text|_start|start_kernel)$' "$ev/final-vmlinux-symbols.txt" ||
    die 'missing_expected_kernel_symbols'
  sha256sum "$out/vmlinux" "$out/System.map" >"$ev/minild-final-vmlinux.sha256"
  printf 'mini_sha=%s\nsource_sha=%s\n' "$(digest "$mini")" "$linux_archive_sha" \
    >"$ev/stage-a.ok"
  echo 'MINILD_RV64_GUARDS=PASS frozen_recursive_make=1 GNU_generated_assembly_only=true Linux_C_compiles=0 all_kernel_LD_is_MiniLD=true'
  echo 'MINILD_RV64_KALLSYMS2=PASS true_Kbuild_mksysmap=true'
  echo 'MINILD_RV64_FINAL_VMLINUX=PASS linker=MiniLD original_link_vmlinux=true System.map_cmp=true C_objects_recompiled=0'
}

image() {
  [[ $# == 3 ]] || die 'usage=image OUT EVIDENCE MINILD'
  local out ev mini
  out=$(realpath "$1"); ev=$(realpath "$2"); mini=$(realpath "$3")
  must_file "$ev/stage-a.ok"
  [[ $(property mini_sha "$ev/stage-a.ok") == "$(digest "$mini")" ]] ||
    die 'MiniLD_binary_changed_since_stage_A'
  [[ $(property source_sha "$ev/stage-a.ok") == "$linux_archive_sha" ]] ||
    die 'Linux_source_changed_since_stage_A'
  must_file "$ev/vmlinux.minild-argv0"
  must_file "$out/vmlinux"
  must_file "$out/System.map"
  must_file "$ev/minild-final-vmlinux.sha256"
  sha256sum -q -c "$ev/minild-final-vmlinux.sha256" || die 'final_vmlinux_changed_since_stage_A'
  verify_frozen_after "$out" "$ev"
  [[ ! -s $out/arch/riscv/boot/Image.minild ]] || die 'image_already_generated'
  # This exact command is OBJCOPYFLAGS_Image from Linux 6.6.143's
  # arch/riscv/boot/Makefile. CONFIG_RELOCATABLE was rejected in stage A.
  riscv64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id \
    -R .comment -S "$out/vmlinux" "$out/arch/riscv/boot/Image.minild" ||
    die 'GNU_objcopy_Image_failed'
  must_file "$out/arch/riscv/boot/Image.minild"
  sha256sum "$out/arch/riscv/boot/Image.minild" >"$ev/minild-Image.sha256"
  echo "MINILD_RV64_IMAGE=PASS linker=MiniLD GNU_objcopy=true size=$(stat -c %s "$out/arch/riscv/boot/Image.minild")"
}

audit_final() {
  [[ $# == 3 ]] || die 'usage=audit-final OUT EVIDENCE MINILD'
  local out ev mini
  out=$(realpath "$1"); ev=$(realpath "$2"); mini=$(realpath "$3")
  must_file "$ev/stage-a.ok"
  [[ $(property mini_sha "$ev/stage-a.ok") == "$(digest "$mini")" ]] ||
    die 'MiniLD_changed_after_QEMU'
  verify_frozen_after "$out" "$ev"
  (cd "$out" && sha256sum -q -c "$ev/minild-final-vmlinux.sha256" &&
    sha256sum -q -c "$ev/minild-Image.sha256") ||
    die 'MiniLD_vmlinux_or_Image_changed_after_QEMU'
  echo 'MINILD_RV64_FINAL_AUDIT=PASS frozen_GNU_kernel_C_objects=true MiniLD_vmlinux_Image_unchanged=true'
}

case "${1:-}" in
  guard-make) shift; guard_make "$@" ;;
  guard-cc) shift; guard_cc "$@" ;;
  guard-ld) shift; guard_ld "$@" ;;
  verify-golden) shift; verify_golden "$@" ;;
  full-link) shift; full_link "$@" ;;
  image) shift; image "$@" ;;
  audit-final) shift; audit_final "$@" ;;
  *) die 'usage: {verify-golden|full-link|image|audit-final|guard-make|guard-cc|guard-ld} ...' ;;
esac
