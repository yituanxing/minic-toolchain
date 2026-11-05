#!/usr/bin/env bash
# Linux 6.6.143 GNU-frozen AArch64 / LoongArch64 complete MiniLD image gate.
# This script NEVER builds a kernel C translation unit. Its only GCC call is
# GNU assembly of fresh, original Kbuild kallsyms-generated .S files.
set -Eeuo pipefail
export LC_ALL=C

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
self="$repo/tools/ci/minild-aarch-loong-cached-full-image-v1.sh"
kernel_sha=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
producer_sha=3107037abc80b0c0019129364693422aee2818aa
producer_run=41856204967
firmware_git_blob=eac998fb49b1d977296269e0b0955084fba9dc4a

die() { printf 'MINILD_TWOARCH_FULL=FAIL %s\n' "$*" >&2; exit 1; }
file() { [[ -s $1 ]] || die "missing_file=$1"; }
digest() { sha256sum "$1" | cut -d' ' -f1; }
enabled() { grep -Fxq "$1=y" "$MINILD_TWO_OUT/include/config/auto.conf"; }
arch_profile() {
  case "$1" in
    aarch64)
      arch=aarch64 srcarch=arm64 prefix=aarch64-linux-gnu
      target_lds=arch/arm64/kernel/vmlinux.lds
      gnu_cc=aarch64-linux-gnu-gcc machine='AArch64' ;;
    loongarch64)
      arch=loongarch64 srcarch=loongarch prefix=loongarch64-linux-gnu
      target_lds=arch/loongarch/kernel/vmlinux.lds
      gnu_cc=loongarch64-linux-gnu-gcc-13 machine='LoongArch' ;;
    *) die "unsupported_arch=$1" ;;
  esac
}

# A frozen genuine GNU kallsyms1 argv unambiguously identifies every linker
# flag preceding --script and the exact archive and static-library operands.
# For Linux 6.6.143 link-vmlinux.sh, the former are KBUILD_LDFLAGS plus
# LDFLAGS_vmlinux concatenated; splitting them differently does not change
# the generated first-link argv. No guessed -m, -pie or erratum flags.
capture_flags() {
  local capture=$1 i script_index=-1 arg
  file "$capture/first-kallsyms1.argv0"
  mapfile -d '' -t original_argv <"$capture/first-kallsyms1.argv0"
  (( ${#original_argv[@]} >= 22 )) || die incomplete_captured_argv
  for ((i=0; i<${#original_argv[@]}; ++i)); do
    if [[ ${original_argv[i]} == "--script=./$target_lds" ]]; then
      (( script_index == -1 )) || die duplicate_kbuild_script
      script_index=$i
    fi
  done
  (( script_index >= 4 )) || die wrong_captured_kernel_script
  local -a expected=("--script=./$target_lds" --strip-debug -o
    .tmp_vmlinux.kallsyms1 --whole-archive vmlinux.a .vmlinux.export.o
    init/version-timestamp.o --no-whole-archive --start-group
    ./drivers/firmware/efi/libstub/lib.a --end-group)
  (( ${#original_argv[@]} == script_index + ${#expected[@]} )) ||
    die unexpected_captured_link_trailing_args
  for ((i=0; i<${#expected[@]}; ++i)); do
    [[ ${original_argv[script_index+i]} == "${expected[i]}" ]] ||
      die "captured_link_operand_mismatch=$i actual=${original_argv[script_index+i]}"
  done
  kbuild_link_flags=$(printf '%s ' "${original_argv[@]:0:script_index}")
  printf 'MINILD_TWOARCH_CAPTURE=PASS arch=%s flags=%s exact_GNU_Kbuild=true\n' \
    "$arch" "$script_index"
}

guard_env() {
  : "${MINILD_TWO_OUT:?}" "${MINILD_TWO_SRC:?}" "${MINILD_TWO_EV:?}"
  : "${MINILD_TWO_MINI:?}" "${MINILD_TWO_CC:?}" "${MINILD_TWO_ARCH:?}"
  [[ $PWD == "$MINILD_TWO_OUT" ]] || die "link_cwd_mismatch=$PWD"
}

guard_make() {
  guard_env
  [[ $# == 4 && $1 == -f && $2 == "$MINILD_TWO_SRC/scripts/Makefile.build" &&
     $3 == obj=init && $4 == init/version-timestamp.o ]] ||
    die "attempted_unfrozen_recursive_Make=$*"
  (cd "$MINILD_TWO_OUT" && sha256sum --quiet -c "$MINILD_TWO_EV/version-timestamp.sha256") ||
    die kernel_C_timestamp_would_recompile
  echo 'init/version-timestamp.o' >>"$MINILD_TWO_EV/make-guard.log"
}

guard_cc() {
  guard_env
  local out='' asm='' next=false has_c=false arg
  for arg in "$@"; do
    if $next; then out=$arg; next=false; continue; fi
    case "$arg" in
      -o) next=true ;;
      -c) has_c=true ;;
      *.S) [[ -z $asm ]] || die multiple_assembler_sources; asm=$arg ;;
      *.c|*.i|*.cc|*.cpp|*.C) die "forbidden_kernel_C_compilation=$arg" ;;
    esac
  done
  $has_c || die GNU_assembler_missing_compile_flag
  case "$out:$asm" in
    .tmp_vmlinux.kallsyms1.o:.tmp_vmlinux.kallsyms1.S|\
    .tmp_vmlinux.kallsyms2.o:.tmp_vmlinux.kallsyms2.S|\
    .tmp_vmlinux.kallsyms3.o:.tmp_vmlinux.kallsyms3.S) ;;
    *) die "unexpected_assembly_output=$out input=$asm" ;;
  esac
  file "$MINILD_TWO_OUT/$asm"
  printf '%s\0' "$@" >"$MINILD_TWO_EV/$out.gnu-as.argv0"
  "$MINILD_TWO_CC" "$@" >"$MINILD_TWO_EV/$out.stdout" \
    2>"$MINILD_TWO_EV/$out.stderr" || die "GNU_generated_assembly_failed=$out"
  file "$MINILD_TWO_OUT/$out"
  echo "$out" >>"$MINILD_TWO_EV/gnu-as-guard.log"
}

guard_ld() {
  guard_env
  local out='' next=false script=false archive=false arg i
  for ((i=1; i<=$#; ++i)); do
    arg=${!i}
    if $next; then out=$arg; next=false; continue; fi
    case "$arg" in
      -o|--output) next=true ;;
      --output=*) out=${arg#*=} ;;
      --script=./arch/*/kernel/vmlinux.lds)
        [[ $arg == "--script=./$MINILD_TWO_LDS" ]] || die wrong_linker_script
        script=true ;;
      vmlinux.a) archive=true ;;
      -r|--relocatable) die unexpected_relocatable_kbuild_link ;;
    esac
  done
  case "$out" in
    .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|\
    .tmp_vmlinux.kallsyms3|vmlinux) ;;
    *) die "unexpected_ld_output=$out" ;;
  esac
  $script && $archive || die non_kernel_link_command
  printf '%s\0' "$@" >"$MINILD_TWO_EV/$out.minild.argv0"
  if [[ $out == .tmp_vmlinux.kallsyms1 ]]; then
    cmp -s "$MINILD_TWO_EV/$out.minild.argv0" \
      "$MINILD_TWO_EV/frozen-first-kallsyms1.argv0" ||
      die first_link_not_exact_GNU_frozen_argv
  fi
  local rc=0
  MINILD_PROFILE_PHASES=1 timeout 240s "$MINILD_TWO_MINI" "$@" \
    >"$MINILD_TWO_EV/$out.stdout" 2>"$MINILD_TWO_EV/$out.stderr" || rc=$?
  if (( rc != 0 )) || [[ ! -s $MINILD_TWO_OUT/$out ]]; then
    tail -n 50 "$MINILD_TWO_EV/$out.stderr" >&2 || true
    die "minild_link_failed=$out status=$rc"
  fi
  echo "$out" >>"$MINILD_TWO_EV/link-guard.log"
  sha256sum "$MINILD_TWO_OUT/$out" >"$MINILD_TWO_EV/$out.sha256"
}

verify_golden() {
  [[ $# == 4 ]] || die 'usage=verify-golden ARCH OUT PRODUCER EVIDENCE'
  arch_profile "$1"
  local out producer ev manifest capture count line matched
  out=$(realpath "$2"); producer=$(realpath "$3")
  ev=$(realpath -m "$4"); mkdir -p "$ev"
  manifest="$producer/ld-evidence/gnu-kbuild-objects.sha256"
  capture="$producer/ld-capture"
  file "$manifest"
  file "$producer/ld-evidence/gnu-kbuild-identity.sha256"
  for line in "$capture/first-kallsyms1.argv0" \
    "$capture/first-kallsyms1.gnu-ld.sha256" "$out/.config" \
    "$out/include/config/auto.conf" "$out/vmlinux" "$out/vmlinux.a" \
    "$out/.vmlinux.export.o" "$out/init/version-timestamp.o" \
    "$out/$target_lds" "$out/drivers/firmware/efi/libstub/lib.a"; do
    file "$line"
  done
  case "$arch" in
    aarch64) file "$out/arch/arm64/boot/Image" ;;
    loongarch64)
      file "$out/arch/loongarch/boot/vmlinuz.efi"
      file "$out/arch/loongarch/boot/zboot-header.o"
      file "$out/arch/loongarch/boot/.vmlinuz.efi.elf.cmd" ;;
  esac
  local expected_ld
  expected_ld=$(command -v "$prefix-ld") || die "missing_GNU_ld=$prefix-ld"
  matched=$(sed -n "s@  $expected_ld\$@@p" "$capture/first-kallsyms1.gnu-ld.sha256")
  [[ $matched =~ ^[0-9a-f]{64}$ && $(digest "$expected_ld") == "$matched" ]] ||
    die GNU_BFD_not_same_as_producer
  (cd "$out" && sha256sum --quiet -c "$manifest") || die GNU_frozen_pool_SHA_mismatch
  sha256sum --quiet -c "$producer/ld-evidence/gnu-kbuild-identity.sha256" ||
    die GNU_original_kernel_identity_mismatch
  count=$(wc -l <"$manifest")
  (( count >= 3000 )) || die "incomplete_GNU_pool_objects=$count"
  python3 - "$manifest" <<'PY'
from pathlib import PurePosixPath
import re, sys
seen = set()
for line in open(sys.argv[1]):
    match = re.fullmatch(r'([a-f0-9]{64})  (\./.+\.(?:o|a))\n?', line)
    if not match: raise SystemExit('MINILD_TWOARCH_FULL=FAIL malformed_GNU_sha_manifest')
    path = PurePosixPath(match[2])
    if path.is_absolute() or '..' in path.parts or path in seen:
        raise SystemExit('MINILD_TWOARCH_FULL=FAIL unsafe_GNU_sha_manifest')
    seen.add(path)
PY
  capture_flags "$capture"
  cp "$manifest" "$ev/verified-original-gnu-objects.sha256"
  # Kallsyms .o are the ONLY mutable GNU kernel objects. All original C,
  # asm, EFI stub, and Loong zboot-header.o / vmlinuz.o are preserved.
  sed '/  \.\/\.tmp_vmlinux\.kallsyms[123]\.o$/d' "$manifest" \
    >"$ev/immutable-gnu-objects.sha256"
  cp "$capture/first-kallsyms1.argv0" "$ev/frozen-first-kallsyms1.argv0"
  (cd "$out" && sha256sum init/version-timestamp.o) >"$ev/version-timestamp.sha256"
  sha256sum "$out/vmlinux.a" "$out/$target_lds" \
    "$out/drivers/firmware/efi/libstub/lib.a" >"$ev/immutable-extra-inputs.sha256"
  sha256sum "$out/vmlinux" >"$ev/original-GNU-vmlinux.sha256"
  case "$arch" in
    aarch64) sha256sum "$out/arch/arm64/boot/Image" >"$ev/original-GNU-image.sha256" ;;
    loongarch64)
      sha256sum "$out/arch/loongarch/boot/vmlinuz.efi" >"$ev/original-GNU-image.sha256"
      sha256sum "$out/arch/loongarch/boot/zboot-header.o" \
        "$out/arch/loongarch/boot/.vmlinuz.efi.elf.cmd" \
        >"$ev/immutable-zboot-inputs.sha256" ;;
  esac
  printf 'producer_run_id=%s\nproducer_sha=%s\noriginal_objects=%s\n' \
    "$producer_run" "$producer_sha" "$count" >"$ev/golden-provenance.txt"
  echo "MINILD_TWOARCH_GOLDEN=PASS arch=$arch original_objects=$count GNU_SHA=true Linux_C_compiles=0"
}

verify_frozen() {
  local out=$1 ev=$2
  (cd "$out" && sha256sum --quiet -c "$ev/immutable-gnu-objects.sha256") ||
    die original_kernel_objects_mutated
  sha256sum --quiet -c "$ev/immutable-extra-inputs.sha256" ||
    die original_link_inputs_mutated
  if [[ -s $ev/immutable-zboot-inputs.sha256 ]]; then
    sha256sum --quiet -c "$ev/immutable-zboot-inputs.sha256" ||
      die original_efi_stub_mutated
  fi
}

verify_source() {
  local src=$1 archive=$2
  file "$archive"
  [[ $(digest "$archive") == "$kernel_sha" ]] || die wrong_linux_archive_SHA
  file "$src/scripts/link-vmlinux.sh"
  file "$src/scripts/mksysmap"
  file "$src/scripts/file-size.sh"
  grep -Fxq 'SUBLEVEL = 143' "$src/Makefile" || die wrong_linux_release
}

full_link() {
  [[ $# == 7 ]] || die 'usage=full-link ARCH OUT SRC ARCHIVE PRODUCER EVIDENCE MINILD'
  arch_profile "$1"
  local out src archive producer ev mini capture includes prefix_flags
  out=$(realpath "$2"); src=$(realpath "$3"); archive=$(realpath "$4")
  producer=$(realpath "$5"); ev=$(realpath "$6"); mini=$(realpath "$7")
  file "$ev/frozen-first-kallsyms1.argv0"
  [[ -x $mini ]] || die missing_minild_executable
  verify_source "$src" "$archive"
  capture="$producer/ld-capture"
  capture_flags "$capture"
  cmp -s "$capture/first-kallsyms1.argv0" \
    "$ev/frozen-first-kallsyms1.argv0" || die wrong_producer_capture
  export MINILD_TWO_OUT="$out" MINILD_TWO_SRC="$src" MINILD_TWO_EV="$ev"
  export MINILD_TWO_MINI="$mini" MINILD_TWO_CC="$gnu_cc"
  export MINILD_TWO_ARCH="$arch" MINILD_TWO_LDS="$target_lds"
  [[ ! -e $ev/full-link.ok ]] || die full_link_already_run
  [[ -x $out/scripts/kallsyms ]] || die frozen_Kbuild_kallsyms_missing
  for prefix_flags in CONFIG_KALLSYMS CONFIG_MODULES CONFIG_EFI_STUB; do
    enabled "$prefix_flags" || die "wrong_frozen_config_missing=$prefix_flags"
  done
  for prefix_flags in CONFIG_DEBUG_INFO_BTF CONFIG_LTO_CLANG CONFIG_VMLINUX_MAP; do
    ! enabled "$prefix_flags" || die "unsupported_frozen_config=$prefix_flags"
  done
  verify_frozen "$out" "$ev"
  sha256sum "$mini" >"$ev/minild-tool.sha256"
  includes="-I$src/arch/$srcarch/include -I$out/arch/$srcarch/include/generated"
  includes+=" -I$src/include -I$out/include"
  includes+=" -I$src/arch/$srcarch/include/uapi -I$out/arch/$srcarch/include/generated/uapi"
  includes+=" -I$src/include/uapi -I$out/include/generated/uapi"
  includes+=" -include $src/include/linux/compiler-version.h"
  includes+=" -include $src/include/linux/kconfig.h"
  export srctree="$src" objtree=. SRCARCH="$srcarch" CONFIG_SHELL=sh
  export KBUILD_LDS="$target_lds"
  export KBUILD_VMLINUX_LIBS=./drivers/firmware/efi/libstub/lib.a
  export NM="$prefix-nm" OBJCOPY="$prefix-objcopy"
  export NOSTDINC_FLAGS=-nostdinc LINUXINCLUDE="$includes"
  export KBUILD_CPPFLAGS="-D__KERNEL__ -fmacro-prefix-map=$src/="
  export KBUILD_AFLAGS_KERNEL='' KBUILD_VERBOSE=1
  case "$arch" in
    aarch64) export KBUILD_AFLAGS='-D__ASSEMBLY__ -fno-PIE -mgeneral-regs-only -mabi=lp64' ;;
    loongarch64) export KBUILD_AFLAGS='-D__ASSEMBLY__ -fno-PIE -mabi=lp64s -msoft-float' ;;
  esac
  export MAKE="bash $self guard-make" CC="bash $self guard-cc"
  export LD="bash $self guard-ld"
  printf 'arch=%s\nKBUILD_LDFLAGS=%s\nKBUILD_LDS=%s\nSRCARCH=%s\n' \
    "$arch" "$kbuild_link_flags" "$target_lds" "$srcarch" >"$ev/full-link-env.txt"
  cd "$out"
  local status=0
  sh "$src/scripts/link-vmlinux.sh" "$LD" "$kbuild_link_flags" '' \
    >"$ev/link-vmlinux.stdout" 2>"$ev/link-vmlinux.stderr" || status=$?
  if (( status != 0 )); then
    tail -n 90 "$ev/link-vmlinux.stderr" >&2 || true
    die "original_link-vmlinux_failed=$status"
  fi
  local ld_order as_order
  file "$ev/link-guard.log"; file "$ev/gnu-as-guard.log"
  [[ $(wc -l <"$ev/make-guard.log") == 1 ]] || die unexpected_Make_invocation_count
  ld_order=$(paste -sd, "$ev/link-guard.log")
  as_order=$(paste -sd, "$ev/gnu-as-guard.log")
  case "$ld_order:$as_order" in
    '.tmp_vmlinux.kallsyms1,.tmp_vmlinux.kallsyms2,vmlinux:.tmp_vmlinux.kallsyms1.o,.tmp_vmlinux.kallsyms2.o'|\
    '.tmp_vmlinux.kallsyms1,.tmp_vmlinux.kallsyms2,.tmp_vmlinux.kallsyms3,vmlinux:.tmp_vmlinux.kallsyms1.o,.tmp_vmlinux.kallsyms2.o,.tmp_vmlinux.kallsyms3.o') ;;
    *) die "unexpected_kernel_link_sequence=LD_{$ld_order}_AS_{$as_order}" ;;
  esac
  file "$out/vmlinux"; file "$out/System.map"
  verify_frozen "$out" "$ev"
  "$prefix-readelf" -W -h "$out/vmlinux" >"$ev/MiniLD-vmlinux-elf-header.txt"
  grep -q "Machine:.*$machine" "$ev/MiniLD-vmlinux-elf-header.txt" ||
    die final_image_wrong_architecture
  sha256sum "$out/vmlinux" "$out/System.map" >"$ev/minild-final-vmlinux.sha256"
  printf 'mini_sha=%s\nlinux_sha=%s\n' "$(digest "$mini")" "$kernel_sha" \
    >"$ev/full-link.ok"
  echo "MINILD_TWOARCH_FULL_LINK=PASS arch=$arch kallsyms=1,2,optional3 final_vmlinux=MiniLD kernel_C_compiles=0"
}

image() {
  [[ $# == 5 ]] || die 'usage=image ARCH OUT SRC EVIDENCE MINILD'
  arch_profile "$1"
  local out src ev mini boot size
  out=$(realpath "$2"); src=$(realpath "$3"); ev=$(realpath "$4")
  mini=$(realpath "$5")
  file "$ev/full-link.ok"
  [[ $(sed -n 's/^mini_sha=//p' "$ev/full-link.ok") == "$(digest "$mini")" ]] ||
    die changed_minild_since_final_link
  sha256sum --quiet -c "$ev/minild-final-vmlinux.sha256" ||
    die modified_final_vmlinux
  verify_frozen "$out" "$ev"
  boot="$out/arch/$srcarch/boot"
  case "$arch" in
    aarch64)
      [[ ! -e $boot/Image.minild ]] || die MiniLD_Image_already_exists
      # Literal arch/arm64/boot/Makefile OBJCOPYFLAGS_Image, Linux 6.6.143.
      "$prefix-objcopy" -O binary -R .note -R .note.gnu.build-id \
        -R .comment -S "$out/vmlinux" "$boot/Image.minild" ||
        die GNU_objcopy_AArch_Image_failed
      file "$boot/Image.minild"
      sha256sum "$boot/Image.minild" >"$ev/minild-image.sha256" ;;
    loongarch64)
      [[ ! -e $boot/vmlinuz.efi.minild ]] || die MiniLD_EFI_already_exists
      grep -Fxq CONFIG_KERNEL_GZIP=y "$out/.config" ||
        die non_GNU_producer_Loong_compression
      grep -Fxq CONFIG_EFI_ZBOOT=y "$out/.config" ||
        die non_GNU_producer_Loong_EFI_ZBOOT
      # arch/loongarch/boot/Makefile: from genuine MiniLD vmlinux, not
      # producer GNU vmlinux. Preserve original GNU boot files untouched.
      "$prefix-objcopy" -O binary --remove-section=.comment \
        --remove-section=.note --remove-section=.options \
        --remove-section=.note.gnu.build-id -S \
        "$out/vmlinux" "$boot/vmlinux.efi.minild" ||
        die Loong_GNU_objcopy_vmlinux_efi_failed
      file "$boot/vmlinux.efi.minild"
      cp "$boot/vmlinux.efi.minild" "$boot/vmlinux.bin.minild"
      # Makefile.zboot's 'hexdump -s16 -n4 -e "%u"' is little-endian;
      # reject corrupt headers and unreasonably large padding targets.
      python3 - "$boot/vmlinux.bin.minild" <<'PY_PAD'
from pathlib import Path
import sys
p = Path(sys.argv[1])
with p.open('r+b') as f:
    header = f.read(20)
    if len(header) != 20:
        raise SystemExit('MINILD_TWOARCH_FULL=FAIL short_Loong_image_header')
    length = int.from_bytes(header[16:20], 'little')
    actual = p.stat().st_size
    if not actual <= length <= 512 * 1024 * 1024:
        raise SystemExit(f'MINILD_TWOARCH_FULL=FAIL invalid_Loong_image_memory_size={length},file={actual}')
    f.truncate(length)
print(f'MINILD_LOONG_ZBOOT_PAD=PASS image_bytes={actual} memory_bytes={length}')
PY_PAD
      gzip -n -f -9 <"$boot/vmlinux.bin.minild" >"$boot/vmlinuz.minild"
      file "$boot/vmlinuz.minild"
      "$prefix-objcopy" -I binary -O elf64-loongarch \
        --rename-section .data=.gzdata,load,alloc,readonly,contents \
        "$boot/vmlinuz.minild" "$boot/vmlinuz.minild.o" ||
        die Loong_GNU_binary_objcopy_failed
      file "$boot/vmlinuz.minild.o"
      # Replay the exact original GNU Kbuild zboot ELF link argv recorded
      # in .vmlinuz.efi.elf.cmd. Python shlex is *data parsing*, never eval;
      # only the generated payload object and output path may change.
      local argvfile="$ev/Loong-zboot.minild.argv0"
      python3 - "$boot/.vmlinuz.efi.elf.cmd" "$src" "$argvfile" <<'PY_ZBOOT'
from pathlib import Path
import shlex, sys
cmdfile, source, evidence = map(Path, sys.argv[1:])
lines = cmdfile.read_text().splitlines()
if not lines or not lines[0].startswith('savedcmd_') or ' := ' not in lines[0]:
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL non_GNU_frozen_zboot_cmd')
argv = shlex.split(lines[0].split(' := ', 1)[1])
if not argv or not argv[0].endswith('/minild-linux-real-final-capture-ld.sh'):
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL missing_original_GNU_zboot_link_wrapper')
argv.pop(0)
original = 'arch/loongarch/boot/vmlinuz.efi.elf'
replacement = 'arch/loongarch/boot/vmlinuz.efi.minild.elf'
old_input = 'arch/loongarch/boot/vmlinuz.o'
new_input = 'arch/loongarch/boot/vmlinuz.minild.o'
if argv.count('-o') != 1 or argv[argv.index('-o') + 1] != original:
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL wrong_GNU_zboot_output')
argv[argv.index('-o') + 1] = replacement
if argv.count(old_input) != 1:
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL wrong_GNU_zboot_payload')
argv[argv.index(old_input)] = new_input
if not any(a.endswith('drivers/firmware/efi/libstub/zboot.lds') for a in argv):
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL missing_GNU_zboot_linker_script')
if not any(a.endswith('arch/loongarch/boot/zboot-header.o') for a in argv):
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL missing_original_GNU_zboot_header')
if not any(a.endswith('drivers/firmware/efi/libstub/lib.a') for a in argv):
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL missing_original_GNU_EFI_libstub')
if any(a in (';', '&&', '|', '>', '<', 'eval') for a in argv):
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL forbidden_shell_operator_in_Kbuild_cmd')
for i, arg in enumerate(argv):
    if arg.endswith('drivers/firmware/efi/libstub/zboot.lds'):
        argv[i] = str(source / 'drivers/firmware/efi/libstub/zboot.lds')
Path(evidence).write_bytes(b'\0'.join(a.encode() for a in argv) + b'\0')
print(f'MINILD_LOONG_ZBOOT_ARGV=PASS original_GNU_cmd=true argc={len(argv)}')
PY_ZBOOT
      local -a args=()
      mapfile -d '' -t args <"$argvfile"
      (cd "$out" && MINILD_PROFILE_PHASES=1 timeout 240s "$mini" "${args[@]}") \
        >"$ev/Loong-zboot-link.stdout" 2>"$ev/Loong-zboot-link.stderr" || {
          tail -n 80 "$ev/Loong-zboot-link.stderr" >&2 || true
          die MiniLD_Loong_EFI_zboot_link_failed
        }
      file "$boot/vmlinuz.efi.minild.elf"
      "$prefix-objcopy" -O binary "$boot/vmlinuz.efi.minild.elf" \
        "$boot/vmlinuz.efi.minild" || die GNU_objcopy_Loong_EFI_failed
      file "$boot/vmlinuz.efi.minild"
      # Kernel Linux 6.6.143 zboot is EFI/PE, not an ELF32 Image.
      python3 - "$boot/vmlinuz.efi.minild" <<'PY_PE'
from pathlib import Path
import struct, sys
b = Path(sys.argv[1]).read_bytes()
if len(b) < 0x100 or b[:2] != b'MZ' or b[2:6] != b'zimg':
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL invalid_Loong_EFI_Zboot_header')
pe = struct.unpack_from('<I', b, 0x3c)[0]
if pe + 6 > len(b) or b[pe:pe+4] != b'PE\0\0':
    raise SystemExit('MINILD_TWOARCH_FULL=FAIL invalid_Loong_PE_header')
machine = struct.unpack_from('<H', b, pe+4)[0]
if machine != 0x6264:  # IMAGE_FILE_MACHINE_LOONGARCH64
    raise SystemExit(f'MINILD_TWOARCH_FULL=FAIL bad_Loong_PE_machine=0x{machine:x}')
print(f'MINILD_LOONG_ZBOOT_PE=PASS bytes={len(b)} machine=0x{machine:04x}')
PY_PE
      sha256sum "$boot/vmlinuz.efi.minild" >"$ev/minild-image.sha256" ;;
  esac
  verify_frozen "$out" "$ev"
  echo "MINILD_TWOARCH_IMAGE=PASS arch=$arch Image_from_MiniLD=true postlink_GNU_objcopy=true EFI_zboot_owner=$([[ $arch == loongarch64 ]] && echo MiniLD || echo not_applicable)"
}

qemu_p1() {
  [[ $# == 5 ]] || die 'usage=qemu-p1 ARCH GNU-or-MINILD OUT WORK EVIDENCE'
  arch_profile "$1"
  local lane=$2 out work ev image initrd log rc=0 qemu args
  [[ $lane == GNU || $lane == MINILD ]] || die "invalid_QEMU_lane=$lane"
  out=$(realpath "$3"); work=$(realpath "$4"); ev=$(realpath "$5")
  initrd="$work/runtime-initramfs.cpio.gz"; file "$initrd"
  case "$arch:$lane" in
    aarch64:GNU) image="$out/arch/arm64/boot/Image" ;;
    aarch64:MINILD) image="$out/arch/arm64/boot/Image.minild" ;;
    loongarch64:GNU) image="$out/arch/loongarch/boot/vmlinuz.efi" ;;
    loongarch64:MINILD) image="$out/arch/loongarch/boot/vmlinuz.efi.minild" ;;
  esac
  file "$image"
  if [[ $lane == GNU ]]; then
    sha256sum --quiet -c "$ev/original-GNU-image.sha256" || die tampered_GNU_QEMU_image
    sha256sum --quiet -c "$ev/original-GNU-vmlinux.sha256" || die tampered_GNU_control_vmlinux
  else
    sha256sum --quiet -c "$ev/minild-image.sha256" || die tampered_MiniLD_QEMU_image
    sha256sum --quiet -c "$ev/minild-final-vmlinux.sha256" || die tampered_MiniLD_QEMU_vmlinux
  fi
  log="$ev/qemu-$arch-$lane-p1.log"
  [[ ! -e $log ]] || die "already_ran_QEMU_lane=$lane"
  if [[ $arch == aarch64 ]]; then
    timeout --signal=TERM "${MINILD_TWO_QEMU_TIMEOUT:-150}" \
      qemu-system-aarch64 -M virt -cpu cortex-a57 -m 1G -smp 1 \
      -nographic -no-reboot -nic none -kernel "$image" -initrd "$initrd" \
      -append 'console=ttyAMA0 earlycon=pl011,0x9000000 loglevel=8 panic=-1 rdinit=/init minic_runtime=p1' \
      </dev/null >"$log" 2>&1 || rc=$?
  else
    local firmware="$work/QEMU_EFI.fd"
    if [[ ! -s $firmware ]]; then
      curl -fsSL --retry 3 \
        https://raw.githubusercontent.com/loongson/Firmware/1c55f43743b947e4c327716afc818ac5c65558ce/LoongArchVirtMachine/QEMU_EFI.fd \
        -o "$firmware"
    fi
    [[ $(git hash-object "$firmware") == "$firmware_git_blob" ]] ||
      die Loong_EFI_firmware_not_authentic
    timeout --signal=TERM "${MINILD_TWO_QEMU_TIMEOUT:-210}" \
      qemu-system-loongarch64 -machine virt -cpu la464 -m 2G -smp 1 \
      -nic none -netdev user,id=loongnet \
      -device virtio-net-pci,netdev=loongnet,romfile= \
      -bios "$firmware" -kernel "$image" -initrd "$initrd" \
      -append 'console=ttyS0 earlycon loglevel=8 panic=-1 rdinit=/init minic_runtime=p1' \
      -display none -serial stdio -monitor none -no-reboot \
      </dev/null >"$log" 2>&1 || rc=$?
  fi
  case "$rc" in
    0|124|143) ;;
    *) tail -n 80 "$log" >&2; die "QEMU_exited_with_error=$rc arch=$arch" ;;
  esac
  for pattern in 'Linux version 6.6.143' 'RUNTIME_V2_SYSCALL_BEGIN' \
    'RUNTIME_V2_SYSCALL_END pass=12 fail=0' \
    'INIT_RUNTIME_PROBE=PASS'; do
    if ! grep -Eq "$pattern" "$log"; then
      tail -n 120 "$log" >&2 || true
      die "QEMU_missing_runtime_marker=$pattern arch=$arch lane=$lane"
    fi
  done
  python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
    --profile p1 --log "$log" || die "QEMU_canonical_P1_failed arch=$arch lane=$lane"
  sha256sum "$image" "$initrd" >"$ev/qemu-$arch-$lane-image-and-initrd.sha256"
  echo "MINILD_TWOARCH_QEMU_P1=PASS arch=$arch lane=$lane original_GNU_control=$([[ $lane == GNU ]] && echo true || echo false) syscalls=12/12 image_sha256=$(digest "$image")"
}

audit_final() {
  [[ $# == 4 ]] || die 'usage=audit-final ARCH OUT EVIDENCE MINILD'
  arch_profile "$1"
  local out ev mini
  out=$(realpath "$2"); ev=$(realpath "$3"); mini=$(realpath "$4")
  file "$ev/full-link.ok"
  [[ $(sed -n 's/^mini_sha=//p' "$ev/full-link.ok") == "$(digest "$mini")" ]] ||
    die final_MiniLD_binary_changed
  verify_frozen "$out" "$ev"
  sha256sum --quiet -c "$ev/minild-final-vmlinux.sha256" || die final_vmlinux_modified
  sha256sum --quiet -c "$ev/minild-image.sha256" || die final_image_modified
  sha256sum --quiet -c "$ev/original-GNU-image.sha256" || die GNU_control_image_modified
  for lane in GNU MINILD; do
    file "$ev/qemu-$arch-$lane-p1.log"
    grep -Eq 'RUNTIME_V2_SYSCALL_END pass=12 fail=0' \
      "$ev/qemu-$arch-$lane-p1.log" || die "missing_final_QEMU_P1=$lane"
    sha256sum --quiet -c "$ev/qemu-$arch-$lane-image-and-initrd.sha256" ||
      die "QEMU_image_or_initrd_mutated=$lane"
  done
  echo "MINILD_TWOARCH_CACHED_FULL_IMAGE=PASS arch=$arch GNU_control_P1=12/12 MiniLD_final_vmlinux=true MiniLD_Image=true MiniLD_QEMU_P1=12/12 kernel_C_recompiled=0"
}

case "${1:-}" in
  preflight-capture)
    [[ $# == 3 ]] || die 'usage=preflight-capture ARCH CAPTURE'
    arch_profile "$2"; capture_flags "$3" ;;
  verify-golden) shift; verify_golden "$@" ;;
  full-link) shift; full_link "$@" ;;
  image) shift; image "$@" ;;
  qemu-p1) shift; qemu_p1 "$@" ;;
  audit-final) shift; audit_final "$@" ;;
  guard-make) shift; guard_make "$@" ;;
  guard-cc) shift; guard_cc "$@" ;;
  guard-ld) shift; guard_ld "$@" ;;
  *) die 'usage={preflight-capture|verify-golden|full-link|image|qemu-p1|audit-final|guard-make|guard-cc|guard-ld}' ;;
esac
