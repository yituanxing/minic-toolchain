#!/usr/bin/env bash
# Linux 6.6.143 LoongArch64: reuse the immutable GNU Kbuild object pool.
# MiniLD links the *final* vmlinux, then the EFI zboot ELF. The original
# Kbuild GNU objcopy/gzip stages package those MiniLD outputs. Kernel C,
# assembly and recursive make are forbidden in this cached replay.
#
#   link  <frozen-out> <minic-ld> <evidence-dir>
#   image <frozen-out> <linux-6.6.143-source> <minic-ld> <evidence-dir>
set -Eeuo pipefail
export LC_ALL=C

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
die() { echo "MINILD_LOONG_FULL=FAIL $*" >&2; exit 1; }
need() { [[ -s $1 ]] || die "missing_input=$1"; }
need_exec() { [[ -x $1 ]] || die "missing_executable=$1"; }

verify_identity() {
  local out=$1
  need "$out/.config"
  need "$out/.vmlinux.cmd"
  need "$out/vmlinux"
  grep -Fxq 'CONFIG_EFI_STUB=y' "$out/.config" || die 'EFI_STUB_disabled'
  grep -Fxq 'CONFIG_EFI_ZBOOT=y' "$out/.config" || die 'EFI_ZBOOT_disabled'
  grep -Fxq 'CONFIG_KALLSYMS=y' "$out/.config" || die 'KALLSYMS_disabled'
  grep -Fxq 'CONFIG_BUILDTIME_TABLE_SORT=y' "$out/.config" ||
    die 'unexpected_Kbuild_sorttable_configuration'
  grep -q 'scripts/link-vmlinux.sh' "$out/.vmlinux.cmd" || die 'no_Kbuild_link_vmlinux_record'
  grep -q -- '-m elf64loongarch' "$out/.vmlinux.cmd" || die 'wrong_GNU_final_emulation'
  readelf -W -h "$out/vmlinux" | grep -Eq 'Machine:.*LoongArch' || die 'wrong_GNU_kernel_machine'
}

freeze_inputs() {
  local out=$1 ev=$2
  mkdir -p "$ev"
  (cd "$out" && sha256sum \
    .config .vmlinux.cmd vmlinux vmlinux.a .vmlinux.export.o \
    init/version-timestamp.o .tmp_vmlinux.kallsyms2.o \
    scripts/sorttable \
    drivers/firmware/efi/libstub/lib.a \
    arch/loongarch/kernel/vmlinux.lds \
    arch/loongarch/boot/vmlinux.efi arch/loongarch/boot/vmlinux.bin \
    arch/loongarch/boot/vmlinuz.efi \
    arch/loongarch/boot/zboot-header.o \
    arch/loongarch/boot/.vmlinux.efi.cmd \
    arch/loongarch/boot/.vmlinux.bin.cmd \
    arch/loongarch/boot/.vmlinuz.cmd \
    arch/loongarch/boot/.vmlinuz.o.cmd \
    arch/loongarch/boot/.vmlinuz.efi.elf.cmd \
    arch/loongarch/boot/.vmlinuz.efi.cmd) >"$ev/frozen-inputs.sha256"
}

verify_inputs() {
  local out=$1 ev=$2
  (cd "$out" && sha256sum --quiet --check "$ev/frozen-inputs.sha256") ||
    die 'frozen_Kbuild_inputs_mutated'
}

extract_section() {
  local elf=$1 section=$2 output=$3 offset_hex size_hex
  # readelf fields are stable relative to the section name even when its
  # numbered prefix is printed as `[ 23]` rather than `[23]`.
  read -r offset_hex size_hex < <(
    readelf -W -S "$elf" |
      awk -v section="$section" '{for (i=1;i<=NF;i++) if ($i==section) {
        print $(i+3), $(i+4); exit
      }}')
  [[ $offset_hex =~ ^[[:xdigit:]]+$ && $size_hex =~ ^[[:xdigit:]]+$ ]] ||
    die "missing_section=$elf:$section"
  (( 16#$size_hex > 0 )) || die "empty_section=$section"
  dd if="$elf" of="$output" bs=1M iflag=skip_bytes,count_bytes \
    skip="$((16#$offset_hex))" count="$((16#$size_hex))" status=none
}

link_final() {
  [[ $# == 3 ]] || die 'usage=link_OUT_MINILD_EVIDENCE'
  local out mini ev rc=0 started elapsed
  out=$(realpath "$1"); mini=$(realpath "$2"); ev=$(realpath -m "$3")
  need_exec "$mini"
  verify_identity "$out"
  freeze_inputs "$out" "$ev"
  sha256sum "$mini" >"$ev/minild-tool.sha256"

  # Reconstructed from Linux 6.6.143 scripts/link-vmlinux.sh, the saved
  # .vmlinux.cmd KBUILD_LDFLAGS/VMLINUX_LDFLAGS and GNU-created inputs.
  # Crucial: *both* .vmlinux.export.o and init/version-timestamp.o are
  # members of --whole-archive. The latter provides the strong linux_banner
  # and changes .rodata by 192 bytes versus an incomplete manual replay.
  local args=(
    -m elf64loongarch -z noexecstack --no-warn-rwx-segments
    -static -n -nostdlib -static -pie --no-dynamic-linker -z notext
    --build-id=sha1 -X --orphan-handling=warn
    --script=arch/loongarch/kernel/vmlinux.lds
    -o "$ev/vmlinux.minild"
    --whole-archive vmlinux.a .vmlinux.export.o init/version-timestamp.o
    --no-whole-archive --start-group
    drivers/firmware/efi/libstub/lib.a --end-group
    .tmp_vmlinux.kallsyms2.o
  )
  printf '%s\0' "${args[@]}" >"$ev/final-vmlinux.minild-argv0"
  printf 'Kbuild_savedcmd=%s\nGNU_frozen_vmlinux=%s\n' \
    "$out/.vmlinux.cmd" "$out/vmlinux" >"$ev/replay-provenance.txt"
  started=$(date +%s%3N)
  (cd "$out" && timeout 240s "$mini" "${args[@]}") \
    >"$ev/final-vmlinux.stdout" 2>"$ev/final-vmlinux.stderr" || rc=$?
  elapsed=$(( $(date +%s%3N) - started ))
  printf '%s\t%s\n' "$elapsed" "$rc" >"$ev/final-vmlinux-timing.tsv"
  [[ $rc == 0 ]] || { tail -n 60 "$ev/final-vmlinux.stderr" >&2; die "final_link_rc=$rc"; }
  need "$ev/vmlinux.minild"
  # Linux scripts/link-vmlinux.sh runs scripts/sorttable after the last LD
  # invocation when CONFIG_BUILDTIME_TABLE_SORT=y. It sorts __ex_table and
  # clears main_extable_sort_needed. The frozen GNU final vmlinux has this
  # post-link processing; omitting it creates a one-byte .init.data diff
  # and an unsorted __ex_table, even though MiniLD emitted correct bytes.
  # Reuse the SHA-frozen original *host tool* (not a GNU linker or compiler).
  need_exec "$out/scripts/sorttable"
  sha256sum "$ev/vmlinux.minild" >"$ev/final-vmlinux-before-sorttable.sha256"
  "$out/scripts/sorttable" "$ev/vmlinux.minild" \
    >"$ev/sorttable.stdout" 2>"$ev/sorttable.stderr" || {
      cat "$ev/sorttable.stderr" >&2
      die 'original_GNU_Kbuild_sorttable_failed'
    }
  sha256sum "$ev/vmlinux.minild" >"$ev/final-vmlinux-after-sorttable.sha256"
  python3 "$repo/tools/ci/minild-final-elf-audit.py" \
    "$out/vmlinux" "$ev/vmlinux.minild" \
    >"$ev/final-vmlinux-audit.stdout" 2>"$ev/final-vmlinux-audit.stderr" || {
      cat "$ev/final-vmlinux-audit.stderr" >&2
      die 'GNU_vs_MiniLD_final_ELF_geometry'
    }
  # Beyond addresses/sizes, verify all relocation bytes and both GNU
  # Kbuild-sorttable-affected sections against the real final GNU vmlinux.
  # .notes build-id/order may legitimately differ, and is not a boot oracle.
  local section label
  for section in .rela.dyn __ex_table .init.data; do
    label=${section#.}
    extract_section "$out/vmlinux" "$section" "$ev/$label.gnu.bin"
    extract_section "$ev/vmlinux.minild" "$section" "$ev/$label.minild.bin"
    cmp -s "$ev/$label.gnu.bin" "$ev/$label.minild.bin" ||
      die "GNU_vs_MiniLD_section_byte_mismatch=$section"
    sha256sum "$ev/$label.gnu.bin" "$ev/$label.minild.bin" \
      >"$ev/$label-byte-exact.sha256"
  done
  # GNU and MiniLD currently place the GNU build-id note in a different
  # order inside .notes, and SHA-1 build IDs need not match. The two Linux
  # notes (func and OPEN) *must* retain their identical ABI payloads.
  readelf -W -n "$out/vmlinux" >"$ev/notes.gnu.txt"
  readelf -W -n "$ev/vmlinux.minild" >"$ev/notes.minild.txt"
  grep -E '^[[:space:]]*Linux[[:space:]]' "$ev/notes.gnu.txt" |
    sort >"$ev/linux-notes.gnu.txt"
  grep -E '^[[:space:]]*Linux[[:space:]]' "$ev/notes.minild.txt" |
    sort >"$ev/linux-notes.minild.txt"
  cmp -s "$ev/linux-notes.gnu.txt" "$ev/linux-notes.minild.txt" ||
    die 'GNU_vs_MiniLD_Linux_note_semantics'
  verify_inputs "$out" "$ev"
  sha256sum "$ev/vmlinux.minild" >"$ev/final-vmlinux.sha256"
  cat "$ev/final-vmlinux-audit.stdout"
  echo "MINILD_LOONG_FINAL=PASS frozen_GNU_Kbuild=true linker=MiniLD original_Kbuild_sorttable=true elapsed_ms=$elapsed GNU_exact_ALLOC_geometry=true GNU_rela_dyn_ex_table_init_data_byte_exact=true GNU_Linux_note_semantics=exact runtime=NOT_RUN"
}

image() {
  [[ $# == 4 ]] || die 'usage=image_OUT_LINUX_SOURCE_MINILD_EVIDENCE'
  local out source mini ev staging size rc=0 started elapsed
  out=$(realpath "$1"); source=$(realpath "$2")
  mini=$(realpath "$3"); ev=$(realpath "$4")
  need_exec "$mini"
  need "$ev/frozen-inputs.sha256"
  need "$ev/final-vmlinux.sha256"
  need "$source/drivers/firmware/efi/libstub/zboot.lds"
  verify_inputs "$out" "$ev"
  sha256sum --quiet --check "$ev/final-vmlinux.sha256" || die 'MiniLD_vmlinux_changed'
  command -v loongarch64-linux-gnu-objcopy >/dev/null || die 'missing_Loong_GNU_objcopy'
  command -v loongarch64-linux-gnu-ld >/dev/null || die 'missing_Loong_GNU_ld_oracle'
  command -v gzip >/dev/null || die 'missing_gzip'
  command -v truncate >/dev/null || die 'missing_truncate'
  # The recorded Kbuild commands are the oracle for each binary stage.
  grep -Fq -- '--remove-section=.note.gnu.build-id -S vmlinux' \
    "$out/arch/loongarch/boot/.vmlinux.efi.cmd" || die 'unexpected_Kbuild_objcopy_recipe'
  grep -Fq -- '--rename-section .data=.gzdata,load,alloc,readonly,contents' \
    "$out/arch/loongarch/boot/.vmlinuz.o.cmd" || die 'unexpected_Kbuild_binary_to_ELF_recipe'
  grep -Fq 'gzip -n -f -9' "$out/arch/loongarch/boot/.vmlinuz.cmd" || die 'unexpected_Kbuild_gzip_recipe'
  grep -Fq -- '-m elf64loongarch' "$out/arch/loongarch/boot/.vmlinuz.efi.elf.cmd" || die 'unexpected_Kbuild_zboot_recipe'
  size=$(sed -n 's/.*truncate -s \([0-9][0-9]*\) arch\/loongarch\/boot\/vmlinux.bin.*/\1/p' \
    "$out/arch/loongarch/boot/.vmlinux.bin.cmd")
  [[ $size =~ ^[0-9]+$ && $size -ge 1048576 ]] || die 'cannot_prove_Kbuild_binary_padding'
  staging="$ev/minild-image-stage"
  mkdir -p "$staging/arch/loongarch/boot"
  # Byte-for-byte flags and binary input paths from the original Kbuild .cmd
  # records; the stage directory creates the _binary_arch_loongarch_boot_*
  # symbols expected by the unchanged frozen zboot-header.o.
  loongarch64-linux-gnu-objcopy -O binary \
    --remove-section=.comment --remove-section=.note \
    --remove-section=.options --remove-section=.note.gnu.build-id \
    -S "$ev/vmlinux.minild" "$staging/arch/loongarch/boot/vmlinux.efi"
  cp "$staging/arch/loongarch/boot/vmlinux.efi" "$staging/arch/loongarch/boot/vmlinux.bin"
  truncate -s "$size" "$staging/arch/loongarch/boot/vmlinux.bin"
  gzip -n -9 -c "$staging/arch/loongarch/boot/vmlinux.bin" >"$staging/arch/loongarch/boot/vmlinuz"
  (cd "$staging" && loongarch64-linux-gnu-objcopy \
    -I binary -O elf64-loongarch \
    --rename-section .data=.gzdata,load,alloc,readonly,contents \
    arch/loongarch/boot/vmlinuz arch/loongarch/boot/vmlinuz.o)
  local zboot=(
    -m elf64loongarch -z noexecstack --no-warn-rwx-segments
    -T "$source/drivers/firmware/efi/libstub/zboot.lds"
    arch/loongarch/boot/vmlinuz.o
    "$out/arch/loongarch/boot/zboot-header.o"
    "$out/drivers/firmware/efi/libstub/lib.a"
    -o arch/loongarch/boot/vmlinuz.efi.elf
  )
  # Before attributing a zboot failure to MiniLD, link the *same* newly
  # compressed input object, original GNU zboot-header.o and libstub archive
  # with the target GNU BFD linker. No new C or assembly is built here.
  local gnu_zboot=("${zboot[@]}") gnu_rc=0
  gnu_zboot[${#gnu_zboot[@]}-1]=arch/loongarch/boot/vmlinuz.efi.gnu.elf
  printf '%s\0' "${gnu_zboot[@]}" >"$ev/zboot.gnu-argv0"
  (cd "$staging" && timeout 240s loongarch64-linux-gnu-ld "${gnu_zboot[@]}") \
    >"$ev/zboot.gnu.stdout" 2>"$ev/zboot.gnu.stderr" || gnu_rc=$?
  [[ $gnu_rc == 0 ]] || {
    tail -n 80 "$ev/zboot.gnu.stderr" >&2
    die "GNU_zboot_control_link_rc=$gnu_rc"
  }
  need "$staging/arch/loongarch/boot/vmlinuz.efi.gnu.elf"
  printf '%s\0' "${zboot[@]}" >"$ev/zboot.minild-argv0"
  started=$(date +%s%3N)
  (cd "$staging" && timeout 240s "$mini" "${zboot[@]}") \
    >"$ev/zboot.stdout" 2>"$ev/zboot.stderr" || rc=$?
  elapsed=$(( $(date +%s%3N) - started ))
  printf '%s\t%s\n' "$elapsed" "$rc" >"$ev/zboot-timing.tsv"
  [[ $rc == 0 ]] || { tail -n 80 "$ev/zboot.stderr" >&2; die "MiniLD_zboot_link_rc=$rc"; }
  need "$staging/arch/loongarch/boot/vmlinuz.efi.elf"
  if ! python3 "$repo/tools/ci/minild-final-elf-audit.py" \
      "$staging/arch/loongarch/boot/vmlinuz.efi.gnu.elf" \
      "$staging/arch/loongarch/boot/vmlinuz.efi.elf" \
      >"$ev/zboot-gnu-vs-minild-audit.stdout" \
      2>"$ev/zboot-gnu-vs-minild-audit.stderr"; then
    cat "$ev/zboot-gnu-vs-minild-audit.stderr" >&2
    die 'GNU_vs_MiniLD_same_input_zboot_ELF_geometry'
  fi
  cat "$ev/zboot-gnu-vs-minild-audit.stdout"
  loongarch64-linux-gnu-objcopy -O binary \
    "$staging/arch/loongarch/boot/vmlinuz.efi.elf" \
    "$staging/arch/loongarch/boot/vmlinuz.efi"
  loongarch64-linux-gnu-objcopy -O binary \
    "$staging/arch/loongarch/boot/vmlinuz.efi.gnu.elf" \
    "$staging/arch/loongarch/boot/vmlinuz.efi.gnu"
  need "$staging/arch/loongarch/boot/vmlinuz.efi"
  need "$staging/arch/loongarch/boot/vmlinuz.efi.gnu"
  readelf -W -h "$staging/arch/loongarch/boot/vmlinuz.efi.elf" \
    >"$ev/zboot-elf-header.txt"
  grep -Eq 'Machine:.*LoongArch' "$ev/zboot-elf-header.txt" || die 'wrong_zboot_machine'
  sha256sum "$staging/arch/loongarch/boot/"{vmlinux.efi,vmlinux.bin,vmlinuz,vmlinuz.o,vmlinuz.efi.gnu.elf,vmlinuz.efi.elf,vmlinuz.efi.gnu,vmlinuz.efi} \
    >"$ev/minild-image-stage.sha256"
  local byte_equal=false
  if cmp -s "$staging/arch/loongarch/boot/vmlinuz.efi.gnu" \
            "$staging/arch/loongarch/boot/vmlinuz.efi"; then
    byte_equal=true
  fi
  printf 'GNU_zboot_same_input=true\nGNU_vs_MiniLD_elf_geometry=exact\nGNU_vs_MiniLD_EFI_binary_byte_equal=%s\nGNU_EFI_bytes=%s\nMiniLD_EFI_bytes=%s\n' \
    "$byte_equal" \
    "$(stat -c %s "$staging/arch/loongarch/boot/vmlinuz.efi.gnu")" \
    "$(stat -c %s "$staging/arch/loongarch/boot/vmlinuz.efi")" \
    >"$ev/zboot-same-input-differential.txt"
  sha256sum "$source/drivers/firmware/efi/libstub/zboot.lds" >"$ev/zboot-script.sha256"
  verify_inputs "$out" "$ev"
  echo "MINILD_LOONG_IMAGE=PASS final_vmlinux=MiniLD EFI_zboot=MiniLD GNU_same_input_zboot_ELF_geometry=exact EFI_binary_byte_equal=$byte_equal GNU_objcopy=true kernel_C_compiles=0 kernel_AS_compiles=0 zboot_ms=$elapsed image=$staging/arch/loongarch/boot/vmlinuz.efi QEMU=NOT_RUN"
}

[[ $# -ge 1 ]] || die 'usage=link_or_image'
case "$1" in
  link) shift; link_final "$@" ;;
  image) shift; image "$@" ;;
  *) die "unknown_action=$1" ;;
esac
