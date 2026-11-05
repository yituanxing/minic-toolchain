#!/usr/bin/env bash
# Opt-in, disposable Linux 6.6.143 RV32 full-link experiment.
# Input: producer 41856204967, SHA-certified GNU Kbuild pool. Never compile kernel C.
# Distinct from RV64: the frozen first-kallsyms1 pool is *not* a
# MiniLD final-vmlinux / Image / RV32 QEMU certificate.
# Linux's original scripts/link-vmlinux.sh owns kallsyms and the final link;
# its three external command boundaries are guarded below.
set -Eeuo pipefail
export LC_ALL=C

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
helper="$repo/tools/ci/minild-rv32-linux-cached-full-image-experiment-v1.sh"
golden_config=4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c
golden_vmlinux=673ed7759599ee434793159a74d0db0ad98258e7a7eff236339931c5ed697426
producer_run=41856204967
producer_sha=3107037abc80b0c0019129364693422aee2818aa
linux_archive_sha=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932

die() { echo "MINILD_RV32_EXPERIMENT=FAIL $*" >&2; exit 1; }
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
  (cd "$MINILD_EXP_OUT" && sha256sum --quiet -c "$MINILD_EXP_EV/version-timestamp.sha256") ||
    die 'frozen_version_timestamp_changed'
  printf '%s\n' 'frozen_existing_init/version-timestamp.o' >>"$MINILD_EXP_EV/make-guard-invocations.log"
  echo 'MINILD_RV32_FROZEN_MAKE=PASS regenerated_kernel_C_objects=0'
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
  echo "MINILD_RV32_GNU_AS=PASS generated=$output source=MiniLD_kallsyms"
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
  if [[ $output == .tmp_vmlinux.kallsyms1 ]]; then
    cmp -s "$MINILD_EXP_EV/$output.minild-argv0" \
      "$MINILD_EXP_EV/frozen-first-kallsyms1.argv0" ||
      die 'Kbuild_first_link_argv_changed_from_exact_producer'
    echo 'MINILD_RV32_FIRST_LINK_ARGV=PASS exact_frozen_GNU_Kbuild_boundary=true'
  fi
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
  echo "MINILD_RV32_LINK=PASS output=$output linker=MiniLD elapsed_ms=$elapsed"
}

# The provenance is the exact GNU Linux 6.6.143 producer, and the source run
# captured its real first-kallsyms1 argv. This is NOT MiniLD final/QEMU proof.
verify_golden() {
  [[ $# == 3 ]] || die 'usage=verify-golden OUT PRODUCER_ROOT EVIDENCE'
  local out producer ev manifest capture count item sha
  out=$(realpath "$1"); producer=$(realpath "$2")
  ev=$(realpath -m "$3"); mkdir -p "$ev"
  manifest="$producer/ld-evidence/gnu-kbuild-objects.sha256"
  capture="$producer/ld-capture"
  for item in "$manifest" "$capture/first-kallsyms1.argv0" \
    "$capture/first-kallsyms1.gnu-ld.sha256" \
    "$out/.config" "$out/include/config/auto.conf" \
    "$out/vmlinux" "$out/arch/riscv/boot/Image" "$out/vmlinux.a" \
    "$out/.vmlinux.export.o" "$out/init/version-timestamp.o" \
    "$out/arch/riscv/kernel/vmlinux.lds"; do must_file "$item"; done
  [[ $(digest "$out/.config") == "$golden_config" ]] ||
    die 'wrong_producer_RV32_kernel_config'
  [[ $(digest "$out/vmlinux") == "$golden_vmlinux" ]] ||
    die 'wrong_original_GNU_vmlinux'
  grep -Fxq 'CONFIG_ARCH_RV32I=y' "$out/include/config/auto.conf" ||
    die 'not_original_RV32_pool'
  local gnu_ld
  gnu_ld=$(command -v riscv64-linux-gnu-ld) || die 'missing_GNU_LD'
  sha=$(sed -n 's@  /usr/bin/riscv64-linux-gnu-ld$@@p' \
    "$capture/first-kallsyms1.gnu-ld.sha256")
  [[ $sha =~ ^[0-9a-f]{64}$ && $(digest "$gnu_ld") == "$sha" ]] ||
    die 'wrong_producer_GNU_LD_binary'
  for item in riscv64-linux-gnu-gcc riscv64-linux-gnu-as \
    riscv64-linux-gnu-nm riscv64-linux-gnu-objcopy \
    riscv64-linux-gnu-readelf; do
    command -v "$item" >/dev/null || die "missing_GNU_tool=$item"
    sha256sum "$(command -v "$item")" >>"$ev/gnu-tool-sha256.txt"
  done
  count=$(wc -l <"$manifest")
  [[ $count =~ ^[0-9]+$ && $count -ge 2000 ]] ||
    die 'implausible_original_GNU_manifest'
  python3 - "$manifest" <<'PY_MANIFEST'
from pathlib import Path
import re
import sys

manifest = Path(sys.argv[1])
seen = set()
for line in manifest.read_text().splitlines():
    m = re.fullmatch(r'([0-9a-f]{64})  (\./.+\.(?:a|o))', line)
    if m is None:
        raise SystemExit('MINILD_RV32_GOLDEN=FAIL malformed_manifest')
    rel = Path(m.group(2))
    if rel.is_absolute() or '..' in rel.parts or str(rel) in seen:
        raise SystemExit('MINILD_RV32_GOLDEN=FAIL unsafe_duplicate_manifest')
    seen.add(str(rel))
PY_MANIFEST
  (cd "$out" && sha256sum --quiet -c "$manifest") ||
    die 'original_GNU_object_sha_mismatch'
  cp "$manifest" "$ev/verified-original-gnu-objects.sha256"
  # Generated kallsyms objects are the ONLY mutable GNU .o across all
  # Kbuild iterations. Preserve every genuine C/asm kernel object and .a.
  sed '/  \.\/\.tmp_vmlinux\.kallsyms[123]\.o$/d' "$manifest" \
    >"$ev/immutable-gnu-objects.sha256"
  (cd "$out" && sha256sum --quiet -c "$ev/immutable-gnu-objects.sha256") ||
    die 'immutable_original_GNU_manifest_failed'
  (cd "$out" && sha256sum init/version-timestamp.o) >"$ev/version-timestamp.sha256"
  sha256sum "$out/vmlinux.a" "$out/.vmlinux.export.o" \
    "$out/arch/riscv/kernel/vmlinux.lds" >"$ev/gnu-frozen-extra-inputs.sha256"
  sha256sum "$out/vmlinux" "$out/arch/riscv/boot/Image" \
    >"$ev/original-gnu-kernel-outputs.sha256"
  cp "$capture/first-kallsyms1.argv0" "$ev/frozen-first-kallsyms1.argv0"
  sha256sum "$ev/frozen-first-kallsyms1.argv0" \
    >"$ev/frozen-first-kallsyms1.sha256"
  printf 'producer_run_id=%s\nproducer_sha=%s\nconfig_sha256=%s\nGNU_vmlinux_sha256=%s\n' \
    "$producer_run" "$producer_sha" "$golden_config" "$golden_vmlinux" \
    >"$ev/verified-frozen-producer.txt"
  echo "MINILD_RV32_GOLDEN=PASS producer_run=$producer_run objects=$count SHA=true config=$golden_config final_Image_QEMU=NOT_YET_TESTED"
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
  (cd "$out" && sha256sum --quiet -c "$ev/immutable-gnu-objects.sha256") ||
    die 'frozen_GNU_objects_mutated'
  (cd "$out" && sha256sum --quiet -c "$ev/version-timestamp.sha256") ||
    die 'frozen_version_timestamp_mutated'
  (cd "$out" && sha256sum --quiet -c "$ev/gnu-frozen-extra-inputs.sha256") ||
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
  must_file "$ev/frozen-first-kallsyms1.argv0"
  must_file "$ev/verified-frozen-producer.txt"
  verify_source "$src" "$archive"
  setup_guard_env "$out" "$src" "$ev" "$mini"
  cd "$out"
  [[ ! -e $ev/stage-a.ok ]] || die 'stage_A_already_run_in_this_job'
  for flags in CONFIG_ARCH_RV32I CONFIG_KALLSYMS CONFIG_MODULES CONFIG_RISCV_SBI; do
    cfg_enabled "$flags" || die "unexpected_kernel_config_disabled=$flags"
  done
  for flags in CONFIG_ARCH_RV64I CONFIG_EFI CONFIG_EFI_STUB \
    CONFIG_RELOCATABLE CONFIG_DEBUG_INFO_BTF CONFIG_LTO_CLANG \
    CONFIG_DYNAMIC_FTRACE CONFIG_LD_DEAD_CODE_DATA_ELIMINATION \
    CONFIG_FPU CONFIG_RISCV_ISA_C CONFIG_RISCV_ISA_V; do
    if cfg_enabled "$flags"; then die "unsupported_pinned_kernel_config=$flags"; fi
  done
  must_exec "$out/scripts/kallsyms"
  if cfg_enabled CONFIG_BUILDTIME_TABLE_SORT; then must_exec "$out/scripts/sorttable"; fi
  verify_frozen_after "$out" "$ev"
  sha256sum "$mini" >"$ev/minild-tool.sha256"
  asm_march=rv32ima
  if cfg_enabled CONFIG_FPU; then asm_march+=fd; fi
  if cfg_enabled CONFIG_RISCV_ISA_C; then asm_march+=c; fi
  if cfg_enabled CONFIG_RISCV_ISA_V; then asm_march+=v; fi
  kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=ilp32 -march=$asm_march"
  if cfg_enabled CONFIG_TOOLCHAIN_NEEDS_OLD_ISA_SPEC; then
    kbuild_asm_flags+=' -Wa,-misa-spec=2.2'
  elif cfg_enabled CONFIG_TOOLCHAIN_NEEDS_EXPLICIT_ZICSR_ZIFENCEI; then
    asm_march+=_zicsr_zifencei
    kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=ilp32 -march=$asm_march"
  fi
  if cfg_enabled CONFIG_TOOLCHAIN_HAS_ZIHINTPAUSE; then
    asm_march+=_zihintpause
    kbuild_asm_flags="-D__ASSEMBLY__ -fno-PIE -mabi=ilp32 -march=$asm_march"
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
  # The producer disabled EFI; a borrowed RV64 libstub would change the
  # original RV32 input sequence. Original first-link argv has no libs.
  export KBUILD_VMLINUX_LIBS=''
  export NM=riscv64-linux-gnu-nm OBJCOPY=riscv64-linux-gnu-objcopy
  export NOSTDINC_FLAGS=-nostdinc
  export LINUXINCLUDE="$kbuild_includes" KBUILD_CPPFLAGS="$cppflags"
  export KBUILD_AFLAGS="$kbuild_asm_flags" KBUILD_AFLAGS_KERNEL=''
  export KBUILD_VERBOSE=1
  export MAKE="bash $helper guard-make"
  export CC="bash $helper guard-cc"
  export LD="bash $helper guard-ld"
  # Exact frozen producer 41856204967 first-kallsyms1 linker invocation.
  local kbuild_ldflags='-melf32lriscv -z noexecstack --no-warn-rwx-segments'
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
  grep -Eq 'Class:[[:space:]]+ELF32' "$ev/final-vmlinux-elf-header.txt" ||
    die 'not_ELF32_kernel'
  grep -Eq 'Machine:.*RISC-V' "$ev/final-vmlinux-elf-header.txt" || die 'not_RISCV_ELF'
  riscv64-linux-gnu-readelf -W -S "$out/vmlinux" >"$ev/final-vmlinux-sections.txt"
  riscv64-linux-gnu-nm "$out/vmlinux" >"$ev/final-vmlinux-symbols.txt"
  grep -Eq ' [Tt] (_text|_start|start_kernel)$' "$ev/final-vmlinux-symbols.txt" ||
    die 'missing_expected_kernel_symbols'
  sha256sum "$out/vmlinux" "$out/System.map" >"$ev/minild-final-vmlinux.sha256"
  printf 'mini_sha=%s\nsource_sha=%s\n' "$(digest "$mini")" "$linux_archive_sha" \
    >"$ev/stage-a.ok"
  echo 'MINILD_RV32_GUARDS=PASS frozen_recursive_make=1 GNU_generated_assembly_only=true Linux_C_compiles=0 all_kernel_LD_is_MiniLD=true'
  echo 'MINILD_RV32_KALLSYMS2=PASS true_Kbuild_mksysmap=true'
  echo 'MINILD_RV32_FINAL_VMLINUX=PASS linker=MiniLD original_link_vmlinux=true System.map_cmp=true C_objects_recompiled=0'
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
  sha256sum --quiet -c "$ev/minild-final-vmlinux.sha256" || die 'final_vmlinux_changed_since_stage_A'
  verify_frozen_after "$out" "$ev"
  [[ ! -s $out/arch/riscv/boot/Image.minild ]] || die 'image_already_generated'
  # This exact command is OBJCOPYFLAGS_Image from Linux 6.6.143's
  # arch/riscv/boot/Makefile. CONFIG_RELOCATABLE was rejected in stage A.
  riscv64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id \
    -R .comment -S "$out/vmlinux" "$out/arch/riscv/boot/Image.minild" ||
    die 'GNU_objcopy_Image_failed'
  must_file "$out/arch/riscv/boot/Image.minild"
  sha256sum "$out/arch/riscv/boot/Image.minild" >"$ev/minild-Image.sha256"
  echo "MINILD_RV32_IMAGE=PASS linker=MiniLD GNU_objcopy=true size=$(stat -c %s "$out/arch/riscv/boot/Image.minild")"
}

audit_final() {
  [[ $# == 3 ]] || die 'usage=audit-final OUT EVIDENCE MINILD'
  local out ev mini
  out=$(realpath "$1"); ev=$(realpath "$2"); mini=$(realpath "$3")
  must_file "$ev/stage-a.ok"
  [[ $(property mini_sha "$ev/stage-a.ok") == "$(digest "$mini")" ]] ||
    die 'MiniLD_changed_after_QEMU'
  verify_frozen_after "$out" "$ev"
  (cd "$out" && sha256sum --quiet -c "$ev/minild-final-vmlinux.sha256" &&
    sha256sum --quiet -c "$ev/minild-Image.sha256") ||
    die 'MiniLD_vmlinux_or_Image_changed_after_QEMU'
  echo 'MINILD_RV32_FINAL_AUDIT=PASS frozen_GNU_kernel_C_objects=true MiniLD_vmlinux_Image_unchanged=true'
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
