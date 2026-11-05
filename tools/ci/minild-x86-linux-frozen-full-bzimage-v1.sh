#!/usr/bin/env bash
# Opt-in Linux 6.6.143 x86-64 linker-only kernel experiment. Never rebuild C.
# Original GNU object/archive pool is authenticated before and after linking.
# MiniLD owns kallsyms1/2[/3], final vmlinux, compressed/vmlinux, setup.elf.
# GNU as is used ONLY for newly generated kallsyms and piggy.S assembly.
set -Eeuo pipefail
export LC_ALL=C

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
self="$repo/tools/ci/minild-x86-linux-frozen-full-bzimage-v1.sh"
linux_sha=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
config_sha=5a50dea34d0a1793c6a8320c15f11b695f1c71fe7b4fbb89bc514f2660ddad65

die() { echo "MINILD_X86_FULL=FAIL $*" >&2; exit 1; }
must_file() { [[ -s $1 ]] || die "missing=$1"; }
digest() { sha256sum "$1" | cut -d' ' -f1; }
cfg() { grep -Fxq "${1}=y" "$MINILD_X86_OUT/include/config/auto.conf"; }
guard_env() {
  : "${MINILD_X86_OUT:?missing OUT}" "${MINILD_X86_EV:?missing EV}"
  : "${MINILD_X86_SRC:?missing SRC}" "${MINILD_X86_MINI:?missing MINI}"
  [[ $PWD == "$MINILD_X86_OUT" ]] || die "guard_not_in_out=$PWD"
}

guard_make() {
  guard_env
  [[ $# == 4 && $1 == -f && $2 == "$MINILD_X86_SRC/scripts/Makefile.build" &&
     $3 == obj=init && $4 == init/version-timestamp.o ]] ||
    die "forbidden_recursive_make=$*"
  (cd "$MINILD_X86_OUT" && sha256sum --quiet -c "$MINILD_X86_EV/version-timestamp.sha256") ||
    die frozen_version_timestamp_changed
  printf '%s\n' init/version-timestamp.o >>"$MINILD_X86_EV/make-guard.log"
}

guard_cc() {
  guard_env
  local source='' output='' after_o=0 have_compile=0 arg
  for arg in "$@"; do
    if ((after_o)); then output=$arg; after_o=0; continue; fi
    case "$arg" in
      -o) after_o=1 ;;
      -c) have_compile=1 ;;
      *.c|*.cc|*.cpp|*.i) die "kernel_C_forbidden=$arg" ;;
      *.S) [[ -z $source ]] || die multiple_generated_assembly; source=$arg ;;
    esac
  done
  ((have_compile)) || die missing_GNU_assembly_flag
  case "$output:$source" in
    .tmp_vmlinux.kallsyms1.o:.tmp_vmlinux.kallsyms1.S|\
    .tmp_vmlinux.kallsyms2.o:.tmp_vmlinux.kallsyms2.S|\
    .tmp_vmlinux.kallsyms3.o:.tmp_vmlinux.kallsyms3.S) ;;
    *) die "unexpected_GNU_AS=$output:$source" ;;
  esac
  must_file "$source"
  printf '%s\0' "$@" >"$MINILD_X86_EV/$output.gnu-as.argv0"
  gcc "$@" >"$MINILD_X86_EV/$output.gnu-as.stdout" \
    2>"$MINILD_X86_EV/$output.gnu-as.stderr" || die "GNU_AS_failed=$output"
  must_file "$output"
  printf '%s\n' "$output" >>"$MINILD_X86_EV/gnu-as-guard.log"
}

guard_ld() {
  guard_env
  local out='' next=0 have_script=0 have_input=0 arg status=0
  for arg in "$@"; do
    if ((next)); then out=$arg; next=0; continue; fi
    case "$arg" in
      -o|--output) next=1 ;;
      --output=*) out=${arg#*=} ;;
      -o?*) out=${arg#-o} ;;
      --script=./arch/x86/kernel/vmlinux.lds) have_script=1 ;;
      vmlinux.o) have_input=1 ;;
      -r|--relocatable) die forbidden_relocatable_kernel_link ;;
    esac
  done
  case "$out" in .tmp_vmlinux.kallsyms1|.tmp_vmlinux.kallsyms2|\
    .tmp_vmlinux.kallsyms3|vmlinux) ;; *) die "unexpected_kernel_LD=$out" ;; esac
  ((have_script && have_input)) || die kernel_LD_contract_missing_script_or_vmlinux_o
  printf '%s\0' "$@" >"$MINILD_X86_EV/$out.minild.argv0"
  timeout 300 "$MINILD_X86_MINI" "$@" >"$MINILD_X86_EV/$out.minild.stdout" \
    2>"$MINILD_X86_EV/$out.minild.stderr" || status=$?
  if ((status)) || [[ ! -s $out ]]; then
    tail -n 50 "$MINILD_X86_EV/$out.minild.stderr" >&2 || true
    die "MiniLD_kernel_link=$out rc=$status"
  fi
  sha256sum "$out" >>"$MINILD_X86_EV/kernel-link-sha256.txt"
  printf '%s\n' "$out" >>"$MINILD_X86_EV/ld-guard.log"
  echo "MINILD_X86_KBUILD_LD=PASS output=$out linker=MiniLD"
}

# Compare generated-header values against what the *frozen* C objects saw.
# The boot header.o and compressed misc.o embed these constants. If a value
# differs, an honest no-C-rebuild pipeline cannot claim a valid full image.
verify_generated_header() {
  local header=$1 elf=$2 prefix=$3 label=$4
  must_file "$header"; must_file "$elf"
  nm "$elf" >"$MINILD_X86_EV/$label.nm"
  python3 - "$header" "$MINILD_X86_EV/$label.nm" "$prefix" "$label" <<'PY'
from pathlib import Path
import re
import sys
header, nm_path, prefix, label = sys.argv[1:]
symbols = {}
for line in Path(nm_path).read_text(errors='replace').splitlines():
    m = re.fullmatch(r'([0-9a-fA-F]+) [A-Za-z] (\S+)', line)
    if m:
        symbols[m.group(2)] = int(m.group(1), 16)
seen = 0
for line in Path(header).read_text().splitlines():
    m = re.match(r'^#define\s+' + re.escape(prefix) + r'(\w+)\s+(.+)$', line)
    if m is None:
        continue
    name, value = m.groups()
    hex_value = re.search(r'0x([0-9a-fA-F]+)', value)
    if not hex_value or name not in symbols:
        raise SystemExit(f'MINILD_X86_{label}=FAIL absent_symbol_or_value={name}')
    expected = int(hex_value.group(1), 16)
    if symbols[name] != expected:
        raise SystemExit(f'MINILD_X86_{label}=FAIL frozen_C_dependency_changed '
                         f'symbol={name} original={expected:#x} MiniLD={symbols[name]:#x}')
    seen += 1
if seen < (4 if label == 'VOFFSET' else 10):
    raise SystemExit(f'MINILD_X86_{label}=FAIL too_few_frozen_constants={seen}')
print(f'MINILD_X86_{label}=PASS frozen_C_rebuilds=0 symbols={seen}')
PY
}

verify_immutable() {
  (cd "$MINILD_X86_OUT" && sha256sum --quiet -c "$MINILD_X86_EV/immutable-gnu-inputs.sha256") ||
    die frozen_GNU_input_changed
  (cd "$MINILD_X86_OUT" && sha256sum --quiet -c "$MINILD_X86_EV/version-timestamp.sha256") ||
    die frozen_version_timestamp_changed
}

# Kbuild's GNU manifest spells paths either `.tmp...` or `./.tmp...`.
# Only the exact outputs regenerated by Linux's kallsyms and piggy recipes
# may be exempt from *post*-run GNU SHA verification. Match normalized
# complete relative paths, never basename patterns that exempt other .o.
project_immutable_manifest() {
  [[ $# == 2 ]] || die 'usage=project-immutable-manifest INPUT OUTPUT'
  python3 - "$1" "$2" <<'PY_MANIFEST'
from pathlib import Path
import re
import sys

source, destination = map(Path, sys.argv[1:])
mutable = {
    '.tmp_vmlinux.kallsyms1.o',
    '.tmp_vmlinux.kallsyms2.o',
    '.tmp_vmlinux.kallsyms3.o',
    'arch/x86/boot/compressed/piggy.o',
}
selected = []
excluded = set()
seen = set()
for line in source.read_text().splitlines():
    match = re.fullmatch(r'[0-9a-f]{64}  (.+\.(?:o|a))', line)
    if not match:
        raise SystemExit('MINILD_X86_FROZEN=FAIL invalid_object_manifest_entry')
    path = Path(match.group(1))
    if path.is_absolute() or '..' in path.parts or path in seen:
        raise SystemExit('MINILD_X86_FROZEN=FAIL unsafe_or_duplicate_object_path')
    seen.add(path)
    if path.as_posix() in mutable:
        excluded.add(path.as_posix())
    else:
        selected.append(line)
if not {'.tmp_vmlinux.kallsyms1.o', '.tmp_vmlinux.kallsyms2.o'} <= excluded:
    raise SystemExit('MINILD_X86_FROZEN=FAIL missing_original_kallsyms_inputs')
if not selected:
    raise SystemExit('MINILD_X86_FROZEN=FAIL no_immutable_inputs')
destination.write_text('\n'.join(selected) + '\n')
print('MINILD_X86_FROZEN=IMMUTABLE_MANIFEST_PASS '
      f'immutable={len(selected)} generated={len(excluded)} '
      f'excluded={",".join(sorted(excluded))}')
PY_MANIFEST
}

preflight() {
  [[ $# == 3 ]] || die 'usage=preflight OUT EVIDENCE MANIFEST'
  export MINILD_X86_OUT MINILD_X86_EV
  MINILD_X86_OUT=$(realpath "$1")
  MINILD_X86_EV=$(realpath -m "$2")
  local manifest=$3
  mkdir -p "$MINILD_X86_EV"
  must_file "$manifest"
  [[ $(digest "$MINILD_X86_OUT/.config") == "$config_sha" ]] || die wrong_pinned_x86_config
  for name in vmlinux.o vmlinux.a .vmlinux.export.o \
      init/version-timestamp.o arch/x86/kernel/vmlinux.lds \
      arch/x86/boot/compressed/vmlinux.lds \
      arch/x86/boot/compressed/mkpiggy arch/x86/boot/tools/build \
      arch/x86/tools/relocs arch/x86/boot/voffset.h arch/x86/boot/zoffset.h \
      arch/x86/boot/setup.bin arch/x86/boot/bzImage \
      arch/x86/boot/compressed/.vmlinux.cmd arch/x86/boot/.setup.elf.cmd; do
    must_file "$MINILD_X86_OUT/$name"
  done
  grep -Fxq CONFIG_X86_KERNEL_IBT=y "$MINILD_X86_OUT/include/config/auto.conf" ||
    die wrong_pinned_x86_IBT_config
  grep -Fxq CONFIG_X86_NEED_RELOCS=y "$MINILD_X86_OUT/include/config/auto.conf" ||
    die unexpected_no_x86_relocs
  grep -Fxq CONFIG_KERNEL_GZIP=y "$MINILD_X86_OUT/include/config/auto.conf" ||
    die unexpected_x86_kernel_compressor
  python3 - "$manifest" <<'PY'
from pathlib import Path
import re
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
if len(lines) < 500:
    raise SystemExit('MINILD_X86_FULL=FAIL GNU_object_manifest_too_small')
for entry in lines:
    m = re.fullmatch(r'[0-9a-f]{64}  (.+\.(?:o|a))', entry)
    if not m or Path(m.group(1)).is_absolute() or '..' in Path(m.group(1)).parts:
        raise SystemExit('MINILD_X86_FULL=FAIL malformed_GNU_object_manifest')
PY
  (cd "$MINILD_X86_OUT" && sha256sum --quiet -c "$manifest") || die original_object_SHA_mismatch
  # These are the only legitimate, dynamically rebuilt .o files in the
  # experiment; every original C-owned object and archive remains immutable.
  project_immutable_manifest "$manifest" \
    "$MINILD_X86_EV/immutable-gnu-inputs.sha256"
  (cd "$MINILD_X86_OUT" && sha256sum init/version-timestamp.o) \
    >"$MINILD_X86_EV/version-timestamp.sha256"
  sha256sum "$MINILD_X86_OUT/vmlinux" "$MINILD_X86_OUT/arch/x86/boot/bzImage" \
    >"$MINILD_X86_EV/original-GNU-outputs.sha256"
  sha256sum "$MINILD_X86_OUT/arch/x86/boot/setup.bin" \
    >"$MINILD_X86_EV/frozen-GNU-setup-bin.sha256"
  cp "$MINILD_X86_OUT/arch/x86/boot/bzImage" "$MINILD_X86_EV/original-GNU-bzImage"
  cp "$MINILD_X86_OUT/arch/x86/boot/voffset.h" "$MINILD_X86_EV/frozen-voffset.h"
  cp "$MINILD_X86_OUT/arch/x86/boot/zoffset.h" "$MINILD_X86_EV/frozen-zoffset.h"
  verify_immutable
  echo "MINILD_X86_GOLDEN=PASS config=$config_sha GNU_production_objects_frozen=true"
}

full_link() {
  [[ $# == 5 ]] || die 'usage=full-link OUT SRC ARCHIVE EVIDENCE MINILD'
  export MINILD_X86_OUT MINILD_X86_SRC MINILD_X86_EV MINILD_X86_MINI
  MINILD_X86_OUT=$(realpath "$1"); MINILD_X86_SRC=$(realpath "$2")
  local archive
  archive=$(realpath "$3")
  MINILD_X86_EV=$(realpath "$4"); MINILD_X86_MINI=$(realpath "$5")
  [[ -x $MINILD_X86_MINI ]] || die missing_MiniLD_binary
  must_file "$MINILD_X86_EV/immutable-gnu-inputs.sha256"
  must_file "$MINILD_X86_SRC/scripts/link-vmlinux.sh"
  [[ $(digest "$archive") == "$linux_sha" ]] || die unexpected_Linux_source_archive
  verify_immutable
  cd "$MINILD_X86_OUT"
  export srctree="$MINILD_X86_SRC" objtree=. SRCARCH=x86 CONFIG_SHELL=sh
  export KBUILD_LDS=arch/x86/kernel/vmlinux.lds KBUILD_VMLINUX_LIBS=''
  export NM=nm OBJCOPY=objcopy
  export NOSTDINC_FLAGS=-nostdinc
  export LINUXINCLUDE="-I$MINILD_X86_SRC/arch/x86/include -I$MINILD_X86_OUT/arch/x86/include/generated -I$MINILD_X86_SRC/include -I$MINILD_X86_OUT/include -I$MINILD_X86_SRC/arch/x86/include/uapi -I$MINILD_X86_OUT/arch/x86/include/generated/uapi -I$MINILD_X86_SRC/include/uapi -I$MINILD_X86_OUT/include/generated/uapi -include $MINILD_X86_SRC/include/linux/compiler-version.h -include $MINILD_X86_SRC/include/linux/kconfig.h"
  export KBUILD_CPPFLAGS='-D__KERNEL__' KBUILD_AFLAGS='-D__ASSEMBLY__ -m64 -fno-PIE'
  export KBUILD_AFLAGS_KERNEL='' KBUILD_VERBOSE=1
  export MAKE="bash $self guard-make" CC="bash $self guard-cc" LD="bash $self guard-ld"
  printf 'src=%s\nout=%s\nmini=%s\n' "$MINILD_X86_SRC" "$PWD" "$MINILD_X86_MINI" \
    >"$MINILD_X86_EV/kernel-kbuild-identity.txt"
  sha256sum "$MINILD_X86_MINI" >"$MINILD_X86_EV/MiniLD-identity.sha256"
  sh "$MINILD_X86_SRC/scripts/link-vmlinux.sh" "$LD" \
    '-m elf_x86_64 -z noexecstack --no-warn-rwx-segments' \
    '--emit-relocs --discard-none -z max-page-size=0x200000 --build-id=sha1 --orphan-handling=error' \
    >"$MINILD_X86_EV/link-vmlinux.stdout" \
    2>"$MINILD_X86_EV/link-vmlinux.stderr" || {
      tail -n 60 "$MINILD_X86_EV/link-vmlinux.stderr" >&2 || true
      die original_Kbuild_link_vmlinux_failed
    }
  for name in .tmp_vmlinux.kallsyms1 .tmp_vmlinux.kallsyms2 vmlinux System.map; do
    must_file "$name"
  done
  [[ $(wc -l <"$MINILD_X86_EV/make-guard.log") == 1 ]] || die unexpected_recursive_Make_count
  grep -Fxq vmlinux "$MINILD_X86_EV/ld-guard.log" || die no_MiniLD_final_vmlinux
  verify_generated_header "$MINILD_X86_EV/frozen-voffset.h" vmlinux VO_ VOFFSET
  # Original x86 arch/x86/Makefile.postlink commands, not an independent
  # replacement format: this requires the MiniLD --emit-relocs ELF sections.
  arch/x86/tools/relocs vmlinux >arch/x86/boot/compressed/vmlinux.relocs \
    2>"$MINILD_X86_EV/postlink-relocs.stderr" || die x86_postlink_relocs_failed
  must_file arch/x86/boot/compressed/vmlinux.relocs
  if ! arch/x86/tools/relocs --abs-relocs vmlinux \
      >"$MINILD_X86_EV/postlink-abs-relocs.stdout" \
      2>"$MINILD_X86_EV/postlink-abs-relocs.stderr"; then
    die forbidden_x86_absolute_relocs
  fi
  [[ ! -s $MINILD_X86_EV/postlink-abs-relocs.stdout &&
     ! -s $MINILD_X86_EV/postlink-abs-relocs.stderr ]] ||
    die x86_postlink_absolute_relocs_present
  objcopy --remove-section='.rel.*' --remove-section='.rel__*' \
    --remove-section='.rela.*' --remove-section='.rela__*' vmlinux ||
    die x86_postlink_objcopy_failed
  verify_immutable
  sha256sum vmlinux System.map arch/x86/boot/compressed/vmlinux.relocs \
    >"$MINILD_X86_EV/MiniLD-final-vmlinux-and-relocs.sha256"
  printf 'mini_sha=%s\n' "$(digest "$MINILD_X86_MINI")" >"$MINILD_X86_EV/full-link.ok"
  echo 'MINILD_X86_FINAL_VMLINUX=PASS original_Kbuild_kallsyms=true linker=MiniLD postlink=PASS frozen_kernel_C=0'
}

# Convert authentic captured GNU Kbuild .cmd to an exact MiniLD invocation.
# No eval/source: a strict Python shlex parser rejects unexpected outputs.
captured_link_args() {
  local cmd=$1 out=$2 evidence=$3
  python3 - "$cmd" "$out" "$MINILD_X86_SRC" "$evidence" <<'PY'
from pathlib import Path
import shlex
import sys
cmdfile, output, source, evidence = sys.argv[1:]
line = Path(cmdfile).read_text().splitlines()[0]
if not line.startswith('savedcmd_') or ' := ' not in line:
    raise SystemExit('MINILD_X86_BOOT=FAIL bad_frozen_kbuild_cmd')
argv = shlex.split(line.split(' := ', 1)[1])
if not argv[0].endswith('/minild-linux-real-final-capture-ld.sh'):
    raise SystemExit('MINILD_X86_BOOT=FAIL not_original_captured_LD_command')
argv = argv[1:]
for idx, arg in enumerate(argv):
    if arg.endswith('/arch/x86/boot/setup.ld'):
        argv[idx] = source + '/arch/x86/boot/setup.ld'
if argv.count('-o') != 1 or argv[argv.index('-o')+1] != output:
    raise SystemExit(f'MINILD_X86_BOOT=FAIL wrong_frozen_link_output={output}')
Path(evidence).write_bytes(b'\0'.join(a.encode() for a in argv) + b'\0')
print(f'MINILD_X86_BOOT_CMD=PASS output={output} args={len(argv)}')
PY
}

mini_boot_link() {
  local name=$1 cmd=$2 output=$3 argv_file="$MINILD_X86_EV/$name.minild.argv0"
  local -a args=()
  captured_link_args "$cmd" "$output" "$argv_file"
  mapfile -d '' -t args <"$argv_file"
  timeout 180 "$MINILD_X86_MINI" "${args[@]}" >"$MINILD_X86_EV/$name.stdout" \
    2>"$MINILD_X86_EV/$name.stderr" || {
      tail -n 50 "$MINILD_X86_EV/$name.stderr" >&2 || true
      die "MiniLD_boot_link_failed=$name"
    }
  must_file "$output"
  sha256sum "$output" >"$MINILD_X86_EV/$name.sha256"
  echo "MINILD_X86_BOOT_LD=PASS output=$output linker=MiniLD"
}

bzimage() {
  [[ $# == 4 ]] || die 'usage=bzimage OUT SRC EVIDENCE MINILD'
  export MINILD_X86_OUT MINILD_X86_SRC MINILD_X86_EV MINILD_X86_MINI
  MINILD_X86_OUT=$(realpath "$1"); MINILD_X86_SRC=$(realpath "$2")
  MINILD_X86_EV=$(realpath "$3"); MINILD_X86_MINI=$(realpath "$4")
  must_file "$MINILD_X86_EV/full-link.ok"
  [[ $(sed -n 's/^mini_sha=//p' "$MINILD_X86_EV/full-link.ok") == \
     "$(digest "$MINILD_X86_MINI")" ]] || die MiniLD_changed_after_final_link
  cd "$MINILD_X86_OUT"
  sha256sum --quiet -c "$MINILD_X86_EV/MiniLD-final-vmlinux-and-relocs.sha256" ||
    die MiniLD_final_kernel_changed
  verify_immutable
  local bz=arch/x86/boot cmp=arch/x86/boot/compressed
  local setup_mode=${MINILD_X86_SETUP_MODE:-mini-all-links}
  local setup_owner output_image
  case "$setup_mode" in
    mini-all-links)
      setup_owner=MiniLD
      output_image="$bz/bzImage.minild" ;;
    staged-gnu-setup)
      setup_owner=GNU_FROZEN
      output_image="$bz/bzImage.minild-kernel-gnu-setup"
      must_file "$MINILD_X86_EV/frozen-GNU-setup-bin.sha256"
      sha256sum --quiet -c "$MINILD_X86_EV/frozen-GNU-setup-bin.sha256" ||
        die frozen_GNU_setup_bin_changed ;;
    *) die "unsupported_setup_link_mode=$setup_mode" ;;
  esac
  # These commands are arch/x86/boot/{compressed/,}Makefile recipes on the
  # pinned source. All 6.6.143 compressed C .o are frozen; only piggy.S is new.
  objcopy -R .comment -S vmlinux "$cmp/vmlinux.bin" || die kernel_to_bin_failed
  (cat "$cmp/vmlinux.bin" "$cmp/vmlinux.relocs" | gzip -n -f -9 >"$cmp/vmlinux.bin.gz") ||
    die kernel_gzip_failed
  "$cmp/mkpiggy" "$cmp/vmlinux.bin.gz" >"$cmp/piggy.S" || die mkpiggy_failed
  must_file "$cmp/piggy.S"
  gcc -m64 -x assembler-with-cpp -c -o "$cmp/piggy.o" "$cmp/piggy.S" \
    >"$MINILD_X86_EV/piggy.gnu-as.stdout" \
    2>"$MINILD_X86_EV/piggy.gnu-as.stderr" || die generated_piggy_GNU_AS_failed
  must_file "$cmp/piggy.o"
  sha256sum "$cmp/piggy.S" "$cmp/piggy.o" >"$MINILD_X86_EV/generated-piggy.sha256"
  mini_boot_link compressed-vmlinux "$cmp/.vmlinux.cmd" "$cmp/vmlinux"
  verify_generated_header "$MINILD_X86_EV/frozen-zoffset.h" "$cmp/vmlinux" ZO_ ZOFFSET
  if [[ $setup_mode == mini-all-links ]]; then
    mini_boot_link setup-elf "$bz/.setup.elf.cmd" "$bz/setup.elf"
    objcopy -O binary "$bz/setup.elf" "$bz/setup.bin" || die MiniLD_setup_to_bin_failed
  else
    # The 16/32-bit setup loader is a *distinct* historical ELF32 GNU-owned
    # component until MiniLD supports the x86 elf_i386 target. No GNU main
    # vmlinux, compressed/vmlinux, or generated piggy object is substituted.
    echo 'MINILD_X86_SETUP=STAGED GNU_frozen_setup_bin=true MiniLD_elf_i386=NOT_RUN'
  fi
  objcopy -O binary -R .note -R .comment -S "$cmp/vmlinux" "$bz/vmlinux.bin" ||
    die MiniLD_compressed_to_bin_failed
  "$bz/tools/build" "$bz/setup.bin" "$bz/vmlinux.bin" "$bz/zoffset.h" \
    "$output_image" || die MiniLD_bzImage_pack_failed
  must_file "$output_image"
  verify_immutable
  sha256sum "$output_image" "$bz/vmlinux.bin" "$bz/setup.bin" \
    "$cmp/vmlinux" >"$MINILD_X86_EV/MiniLD-bzImage-and-boot-chain.sha256"
  printf 'setup_mode=%s\nsetup_linker=%s\nimage_path=%s\n' \
    "$setup_mode" "$setup_owner" "$output_image" \
    >"$MINILD_X86_EV/boot-ownership.txt"
  if [[ $setup_mode == mini-all-links ]]; then
    echo "MINILD_X86_BZIMAGE=PASS all_kernel_and_setup_links=MiniLD Linux_C_rebuilt=0 bytes=$(stat -c%s "$output_image")"
  else
    echo "MINILD_X86_BZIMAGE=STAGED_PASS kernel_link=MiniLD compressed_link=MiniLD setup_link=GNU_FROZEN Linux_C_rebuilt=0 bytes=$(stat -c%s "$output_image")"
  fi
}

case "${1:-}" in
  guard-make) shift; guard_make "$@" ;;
  guard-cc) shift; guard_cc "$@" ;;
  guard-ld) shift; guard_ld "$@" ;;
  preflight) shift; preflight "$@" ;;
  full-link) shift; full_link "$@" ;;
  bzimage) shift; bzimage "$@" ;;
  check-header)
    shift
    [[ $# == 5 ]] || die 'usage=check-header HEADER ELF PREFIX LABEL EVIDENCE_DIR'
    export MINILD_X86_EV
    MINILD_X86_EV=$(realpath -m "$5")
    mkdir -p "$MINILD_X86_EV"
    verify_generated_header "$1" "$2" "$3" "$4" ;;
  check-captured)
    shift
    [[ $# == 4 ]] || die 'usage=check-captured CMD_FILE OUTPUT SOURCE ARGV0'
    export MINILD_X86_SRC
    MINILD_X86_SRC=$(realpath "$3")
    captured_link_args "$1" "$2" "$4" ;;
  project-immutable-manifest)
    shift; project_immutable_manifest "$@" ;;
  *) die 'usage={preflight|full-link|bzimage|guard-make|guard-cc|guard-ld}' ;;
esac
