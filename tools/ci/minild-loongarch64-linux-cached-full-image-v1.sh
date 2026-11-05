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

extract_rela_dyn() {
  local elf=$1 output=$2 offset_hex size_hex
  # readelf fields are stable relative to the section name even when its
  # numbered prefix is printed as `[ 23]` rather than `[23]`.
  read -r offset_hex size_hex < <(
    readelf -W -S "$elf" |
      awk '{for (i=1;i<=NF;i++) if ($i==".rela.dyn") {
        print $(i+3), $(i+4); exit
      }}')
  [[ $offset_hex =~ ^[[:xdigit:]]+$ && $size_hex =~ ^[[:xdigit:]]+$ ]] ||
    die "missing_rela_dyn=$elf"
  (( 16#$size_hex > 0 )) || die 'empty_rela_dyn'
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
  python3 "$repo/tools/ci/minild-final-elf-audit.py" \
    "$out/vmlinux" "$ev/vmlinux.minild" \
    >"$ev/final-vmlinux-audit.stdout" 2>"$ev/final-vmlinux-audit.stderr" || {
      cat "$ev/final-vmlinux-audit.stderr" >&2
      die 'GNU_vs_MiniLD_final_ELF_geometry'
    }
  # The LoongArch PIE kernel consumes its relocation payload at boot. A
  # correct .rela.dyn size/address is insufficient: compare *all* 2.7 MiB
  # of real relocations with GNU byte for byte, not just sampled entries.
  extract_rela_dyn "$out/vmlinux" "$ev/rela.dyn.gnu.bin"
  extract_rela_dyn "$ev/vmlinux.minild" "$ev/rela.dyn.minild.bin"
  cmp -s "$ev/rela.dyn.gnu.bin" "$ev/rela.dyn.minild.bin" ||
    die 'GNU_vs_MiniLD_rela_dyn_byte_mismatch'
  sha256sum "$ev/rela.dyn.gnu.bin" "$ev/rela.dyn.minild.bin" \
    >"$ev/rela-dyn-byte-exact.sha256"
  verify_inputs "$out" "$ev"
  sha256sum "$ev/vmlinux.minild" >"$ev/final-vmlinux.sha256"
  cat "$ev/final-vmlinux-audit.stdout"
  echo "MINILD_LOONG_FINAL=PASS frozen_GNU_Kbuild=true linker=MiniLD elapsed_ms=$elapsed GNU_exact_ALLOC_geometry=true GNU_rela_dyn_byte_exact=true runtime=NOT_RUN"
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
  printf '%s\0' "${zboot[@]}" >"$ev/zboot.minild-argv0"
  started=$(date +%s%3N)
  (cd "$staging" && timeout 240s "$mini" "${zboot[@]}") \
    >"$ev/zboot.stdout" 2>"$ev/zboot.stderr" || rc=$?
  elapsed=$(( $(date +%s%3N) - started ))
  printf '%s\t%s\n' "$elapsed" "$rc" >"$ev/zboot-timing.tsv"
  [[ $rc == 0 ]] || { tail -n 80 "$ev/zboot.stderr" >&2; die "MiniLD_zboot_link_rc=$rc"; }
  need "$staging/arch/loongarch/boot/vmlinuz.efi.elf"
  loongarch64-linux-gnu-objcopy -O binary \
    "$staging/arch/loongarch/boot/vmlinuz.efi.elf" \
    "$staging/arch/loongarch/boot/vmlinuz.efi"
  need "$staging/arch/loongarch/boot/vmlinuz.efi"
  readelf -W -h "$staging/arch/loongarch/boot/vmlinuz.efi.elf" \
    >"$ev/zboot-elf-header.txt"
  grep -Eq 'Machine:.*LoongArch' "$ev/zboot-elf-header.txt" || die 'wrong_zboot_machine'
  sha256sum "$staging/arch/loongarch/boot/"{vmlinux.efi,vmlinux.bin,vmlinuz,vmlinuz.o,vmlinuz.efi.elf,vmlinuz.efi} \
    >"$ev/minild-image-stage.sha256"
  sha256sum "$source/drivers/firmware/efi/libstub/zboot.lds" >"$ev/zboot-script.sha256"
  verify_inputs "$out" "$ev"
  echo "MINILD_LOONG_IMAGE=PASS final_vmlinux=MiniLD EFI_zboot=MiniLD GNU_objcopy=true kernel_C_compiles=0 kernel_AS_compiles=0 zboot_ms=$elapsed image=$staging/arch/loongarch/boot/vmlinuz.efi QEMU=NOT_RUN"
}

[[ $# -ge 1 ]] || die 'usage=link_or_image'
case "$1" in
  link) shift; link_final "$@" ;;
  image) shift; image "$@" ;;
  *) die "unknown_action=$1" ;;
esac
