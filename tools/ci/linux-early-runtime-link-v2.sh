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
LD=${cross}ld
NM=${cross}nm
OBJCOPY=${cross}objcopy
OBJDUMP=${cross}objdump
expected_config_sha=e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7

for tool in "$AR" "$LD" "$NM" "$OBJCOPY" "$OBJDUMP"; do command -v "$tool" >/dev/null; done
for path in \
  "$src/scripts/head-object-list.txt" \
  "$src/arch/riscv/tools/relocs_check.sh" \
  "$out/.config" "$out/include/config/auto.conf" \
  "$out/lib/lib.a" "$out/built-in.a" "$out/arch/riscv/lib/lib.a" \
  "$out/drivers/firmware/efi/libstub/lib.a" \
  "$out/arch/riscv/kernel/vmlinux.lds" "$out/init/version-timestamp.o" \
  "$out/lib/idr.o" "$out/lib/xarray.o"; do
  test -e "$path" || { echo "EARLY_LINK_MISSING=$path" >&2; exit 65; }
done

actual_config_sha=$(sha256sum "$out/.config" | awk '{print $1}')
[[ "$actual_config_sha" == "$expected_config_sha" ]] || {
  echo "EARLY_LINK_FIXTURE_MISMATCH expected=$expected_config_sha actual=$actual_config_sha" >&2
  exit 66
}
if grep -q '^CONFIG_DEBUG_INFO_BTF=y' "$out/include/config/auto.conf"; then
  echo 'EARLY_LINK_UNSUPPORTED=CONFIG_DEBUG_INFO_BTF' >&2
  exit 67
fi
printf 'EARLY_LINK_FIXTURE=PASS config_sha=%s\n' "$actual_config_sha" | tee "$ev/fixture.txt"

cd "$out"

# lib/lib.a is a thin archive.  The certified fixture already has the exact
# member ordering we need; replacing lib/idr.o or lib/xarray.o changes what the
# archive resolves to without requiring an O(n) archive rebuild.  Verify the
# contract instead of mutating the archive.
verify_start=$(date +%s%N)
lib_archive_sha_before=$(sha256sum lib/lib.a | awk '{print $1}')
"$AR" t lib/lib.a >"$ev/lib.members.txt"
grep -Fxq 'lib/idr.o' "$ev/lib.members.txt"
grep -Fxq 'lib/xarray.o' "$ev/lib.members.txt"
while IFS= read -r member; do
  [[ -z "$member" ]] && continue
  test -e "$member" || { echo "EARLY_LINK_MISSING_MEMBER=$member" >&2; exit 69; }
done <"$ev/lib.members.txt"
lib_archive_sha_after=$(sha256sum lib/lib.a | awk '{print $1}')
[[ "$lib_archive_sha_before" == "$lib_archive_sha_after" ]] || {
  echo 'EARLY_LINK_ARCHIVE_MUTATED=lib/lib.a' >&2
  exit 70
}
verify_end=$(date +%s%N)
echo "TIMING early_verify_ms=$(((verify_end-verify_start)/1000000))" | tee "$ev/timing.txt"

archive_start=$(date +%s%N)
rm -f vmlinux.a
"$AR" cDPrST vmlinux.a ./built-in.a lib/lib.a arch/riscv/lib/lib.a
first_member=$("$AR" t vmlinux.a | sed -n '1p')
mapfile -t head_members < <("$AR" t vmlinux.a | grep -F -f "$src/scripts/head-object-list.txt" || true)
if [[ -n "$first_member" && ${#head_members[@]} -gt 0 ]]; then
  "$AR" mPiT "$first_member" vmlinux.a "${head_members[@]}"
fi
archive_end=$(date +%s%N)
echo "TIMING early_vmlinux_archive_ms=$(((archive_end-archive_start)/1000000))" | tee -a "$ev/timing.txt"

link_start=$(date +%s%N)
"$LD" \
  -melf64lriscv -z noexecstack --no-warn-rwx-segments \
  -z norelro --build-id=sha1 --orphan-handling=warn \
  --script=arch/riscv/kernel/vmlinux.lds --strip-debug \
  -o "$ev/vmlinux.early" \
  --whole-archive vmlinux.a init/version-timestamp.o --no-whole-archive \
  --start-group ./drivers/firmware/efi/libstub/lib.a --end-group \
  >"$ev/ld.stdout.txt" 2>"$ev/ld.stderr.txt"
link_end=$(date +%s%N)
echo "TIMING early_ld_ms=$(((link_end-link_start)/1000000))" | tee -a "$ev/timing.txt"

post_start=$(date +%s%N)
if grep -q '^CONFIG_RELOCATABLE=y' include/config/auto.conf; then
  /bin/sh "$src/arch/riscv/tools/relocs_check.sh" "$OBJDUMP" "$NM" "$ev/vmlinux.early"
  "$OBJCOPY" \
    --remove-section='.rel.*' --remove-section='.rel__*' \
    --remove-section='.rela.*' --remove-section='.rela__*' \
    "$ev/vmlinux.early"
fi
"$OBJCOPY" -O binary \
  -R .note -R .note.gnu.build-id -R .comment -S \
  "$ev/vmlinux.early" "$ev/Image.early"
post_end=$(date +%s%N)
echo "TIMING early_postlink_ms=$(((post_end-post_start)/1000000))" | tee -a "$ev/timing.txt"

test -s "$ev/vmlinux.early"
test -s "$ev/Image.early"
"$NM" -n "$ev/vmlinux.early" >"$ev/nm.txt"
sha256sum "$ev/vmlinux.early" "$ev/Image.early" | tee "$ev/sha256.txt"
total_ms=$(((post_end-verify_start)/1000000))
echo "TIMING early_total_ms=$total_ms" | tee -a "$ev/timing.txt"
echo 'EARLY_LINK_V2=PASS'
