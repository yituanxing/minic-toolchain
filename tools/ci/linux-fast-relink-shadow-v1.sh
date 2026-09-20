#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <linux-src> <linux-out> <evidence-dir>" >&2
  exit 64
fi

src=$(realpath "$1")
out=$(realpath "$2")
ev=$(realpath -m "$3")
mkdir -p "$ev"

cross=${CROSS_COMPILE:-riscv64-linux-gnu-}
AR=${cross}ar
CC=${cross}gcc
LD=${cross}ld
NM=${cross}nm
OBJCOPY=${cross}objcopy
OBJDUMP=${cross}objdump

expected_config_sha=e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7

for tool in "$AR" "$CC" "$LD" "$NM" "$OBJCOPY" "$OBJDUMP"; do
  command -v "$tool" >/dev/null
 done

for path in \
  "$src/scripts/link-vmlinux.sh" \
  "$src/scripts/mksysmap" \
  "$src/arch/riscv/tools/relocs_check.sh" \
  "$out/.config" \
  "$out/include/config/auto.conf" \
  "$out/lib/lib.a" \
  "$out/built-in.a" \
  "$out/arch/riscv/lib/lib.a" \
  "$out/drivers/firmware/efi/libstub/lib.a" \
  "$out/arch/riscv/kernel/vmlinux.lds" \
  "$out/init/version-timestamp.o" \
  "$out/scripts/mod/modpost" \
  "$out/scripts/kallsyms" \
  "$out/scripts/sorttable"; do
  test -e "$path" || { echo "FAST_RELINK_MISSING=$path" >&2; exit 65; }
done

actual_config_sha=$(sha256sum "$out/.config" | awk '{print $1}')
if [[ "$actual_config_sha" != "$expected_config_sha" ]]; then
  echo "FAST_RELINK_FIXTURE_MISMATCH expected_config_sha=$expected_config_sha actual_config_sha=$actual_config_sha" >&2
  exit 66
fi
if grep -q '^CONFIG_DEBUG_INFO_BTF=y' "$out/include/config/auto.conf"; then
  echo 'FAST_RELINK_UNSUPPORTED=CONFIG_DEBUG_INFO_BTF' >&2
  exit 67
fi

printf 'FAST_RELINK_FIXTURE=PASS config_sha=%s\n' "$actual_config_sha" | tee "$ev/fixture.txt"

cd "$out"

# Rebuild the thin lib archive from its current member order. This is the
# only subtree touched by the current IDR/XArray frontier and refreshes the
# archive symbol index without descending through Kbuild again.
"$AR" t lib/lib.a >"$ev/lib.members.before.txt"
mapfile -t lib_members <"$ev/lib.members.before.txt"
if [[ ${#lib_members[@]} -eq 0 ]]; then
  echo 'FAST_RELINK_EMPTY_ARCHIVE=lib/lib.a' >&2
  exit 68
fi
for member in "${lib_members[@]}"; do
  test -e "$member" || { echo "FAST_RELINK_MISSING_MEMBER=$member" >&2; exit 69; }
done
rm -f lib/lib.a
"$AR" cDPrsT lib/lib.a "${lib_members[@]}"
"$AR" t lib/lib.a >"$ev/lib.members.after.txt"
cmp -s "$ev/lib.members.before.txt" "$ev/lib.members.after.txt"

# Recreate vmlinux.a exactly as the pinned RISC-V 6.6.143 fixture does.
rm -f vmlinux.a
"$AR" cDPrST vmlinux.a ./built-in.a lib/lib.a arch/riscv/lib/lib.a
first_member=$("$AR" t vmlinux.a | sed -n '1p')
mapfile -t head_members < <("$AR" t vmlinux.a | grep -F -f "$src/scripts/head-object-list.txt" || true)
if [[ -n "$first_member" && ${#head_members[@]} -gt 0 ]]; then
  "$AR" mPiT "$first_member" vmlinux.a "${head_members[@]}"
fi
"$AR" t vmlinux.a >"$ev/vmlinux-a.members.txt"

# Recreate the vmlinux.o/modpost terminal stage without walking the full
# recursive directory graph.
"$LD" -melf64lriscv -z noexecstack --no-warn-rwx-segments \
  -r -o vmlinux.o \
  --whole-archive vmlinux.a --no-whole-archive \
  --start-group ./drivers/firmware/efi/libstub/lib.a --end-group

"$OBJCOPY" -j .modinfo -O binary vmlinux.o modules.builtin.modinfo
tr '\0' '\n' < modules.builtin.modinfo \
  | sed -n 's/^[[:alnum:]:_]*\.file=//p' \
  | tr ' ' '\n' | uniq | sed -e 's:^:kernel/:' -e 's/$/.ko/' \
  > modules.builtin
scripts/mod/modpost -M -o vmlinux.symvers vmlinux.o \
  >"$ev/modpost.stdout.txt" 2>"$ev/modpost.stderr.txt"

# link-vmlinux.sh is retained verbatim for kallsyms, System.map and table
# sorting. MAKE=true deliberately suppresses only its version-timestamp
# rebuild: the reference path has just produced the certified object and the
# shadow must consume exactly that same input.
export srctree="$src"
export objtree="$out"
export ARCH=riscv
export SRCARCH=riscv
export CC
export LD
export NM
export OBJCOPY
export OBJDUMP
export CONFIG_SHELL=/bin/sh
export KBUILD_VMLINUX_LIBS='./drivers/firmware/efi/libstub/lib.a'
export KBUILD_LDS='arch/riscv/kernel/vmlinux.lds'
export NOSTDINC_FLAGS='-nostdinc'
export LINUXINCLUDE="-I${src}/arch/riscv/include -I${out}/arch/riscv/include/generated -I${src}/include -I${out}/include -I${src}/arch/riscv/include/uapi -I${out}/arch/riscv/include/generated/uapi -I${src}/include/uapi -I${out}/include/generated/uapi -include ${src}/include/linux/compiler-version.h -include ${src}/include/linux/kconfig.h"
export KBUILD_CPPFLAGS="-D__KERNEL__ -fmacro-prefix-map=${src}/="
export KBUILD_AFLAGS='-D__ASSEMBLY__ -fno-PIE -mabi=lp64 -march=rv64imafdcv_zicsr_zifencei_zihintpause -mno-riscv-attribute -Wa,-mno-arch-attr'
export KBUILD_AFLAGS_KERNEL=''
export KBUILD_VERBOSE=1
export MAKE=true

"$src/scripts/link-vmlinux.sh" \
  "$LD" \
  '-melf64lriscv -z noexecstack --no-warn-rwx-segments' \
  '-z norelro --build-id=sha1 --orphan-handling=warn' \
  >"$ev/link-vmlinux.stdout.txt" 2>"$ev/link-vmlinux.stderr.txt"

if grep -q '^CONFIG_RELOCATABLE=y' include/config/auto.conf; then
  "$CONFIG_SHELL" "$src/arch/riscv/tools/relocs_check.sh" "$OBJDUMP" "$NM" vmlinux
  cp vmlinux vmlinux.relocs
  "$OBJCOPY" \
    --remove-section='.rel.*' \
    --remove-section='.rel__*' \
    --remove-section='.rela.*' \
    --remove-section='.rela__*' vmlinux
fi

"$OBJCOPY" -O binary \
  -R .note -R .note.gnu.build-id -R .comment -S \
  vmlinux arch/riscv/boot/Image

test -s vmlinux
test -s arch/riscv/boot/Image
sha256sum vmlinux arch/riscv/boot/Image | tee "$ev/fast.sha256"
"$NM" -n vmlinux >"$ev/fast.nm.txt"
echo 'FAST_RELINK=PASS'
