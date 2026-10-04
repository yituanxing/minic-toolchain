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
CC=${cross}gcc
expected_config_sha=e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7
kallsyms_src=${EARLY_LINK_KALLSYMS_SRC:-$src}
with_kallsyms=${EARLY_LINK_WITH_KALLSYMS:-0}

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

is_enabled() {
  grep -q "^$1=y" include/config/auto.conf
}

# lib/lib.a is a thin archive. The certified fixture already has the exact
# member ordering we need; replacing lib/idr.o or lib/xarray.o changes what the
# archive resolves to without requiring an O(n) archive rebuild. Verify the
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

# Link one image using the exact same core archive/library set as the old V2.
# Optional trailing arguments are extra objects, notably the generated kallsyms
# data object. This mirrors vmlinux_link() in Linux scripts/link-vmlinux.sh.
link_one() {
  local output=$1
  shift
  "$LD" \
    -melf64lriscv -z noexecstack --no-warn-rwx-segments \
    -z norelro --build-id=sha1 --orphan-handling=warn \
    --script=arch/riscv/kernel/vmlinux.lds --strip-debug \
    -Map="${output}.map" \
    -o "$output" \
    --whole-archive vmlinux.a init/version-timestamp.o --no-whole-archive \
    --start-group ./drivers/firmware/efi/libstub/lib.a --end-group \
    "$@"
}

# Build the generated kallsyms data object from a temporary vmlinux, following
# Linux 6.6 scripts/link-vmlinux.sh. The early-runtime fixture is pinned to a
# 64-bit RISC-V config, so replacing the single bitsperlong include with the
# already-known value avoids depending on the full kernel assembler include
# stack merely to assemble a data-only generated file.
kallsyms_step() {
  local step=$1
  local prev=${2:-}
  local tmp="$ev/.tmp_vmlinux.kallsyms${step}"
  local syms="${tmp}.syms"
  local asm="${tmp}.S"
  local obj="${tmp}.o"
  local -a opts=()

  if is_enabled CONFIG_KALLSYMS_ALL; then opts+=(--all-symbols); fi
  if is_enabled CONFIG_KALLSYMS_ABSOLUTE_PERCPU; then opts+=(--absolute-percpu); fi
  if is_enabled CONFIG_KALLSYMS_BASE_RELATIVE; then opts+=(--base-relative); fi
  if is_enabled CONFIG_LTO_CLANG; then opts+=(--lto-clang); fi

  if [[ -n "$prev" ]]; then
    link_one "$tmp" "$prev" >"${tmp}.ld.stdout.txt" 2>"${tmp}.ld.stderr.txt"
    NM="$NM" /bin/sh "$kallsyms_src/scripts/mksysmap" "$tmp" "$syms" "$prev" \
      >"${tmp}.mksysmap.stdout.txt" 2>"${tmp}.mksysmap.stderr.txt"
  else
    link_one "$tmp" >"${tmp}.ld.stdout.txt" 2>"${tmp}.ld.stderr.txt"
    NM="$NM" /bin/sh "$kallsyms_src/scripts/mksysmap" "$tmp" "$syms" \
      >"${tmp}.mksysmap.stdout.txt" 2>"${tmp}.mksysmap.stderr.txt"
  fi

  "$out/scripts/kallsyms" "${opts[@]}" "$syms" >"$asm"
  # scripts/kallsyms emits only data directives plus PTR/ALGN macros selected
  # from BITS_PER_LONG. The certified fixture is RV64; make that preprocessing
  # fact explicit and assemble the generated data object.
  sed -i 's@^#include <asm/bitsperlong.h>@#define BITS_PER_LONG 64@' "$asm"
  "$CC" -c -x assembler-with-cpp -o "$obj" "$asm"
  test -s "$obj"
  echo "$obj"
}

link_start=$(date +%s%N)
extra_kallsyms_obj=""
last_kallsyms_syms=""
if [[ "$with_kallsyms" == 1 ]] && is_enabled CONFIG_KALLSYMS; then
  command -v "$CC" >/dev/null
  test -x "$out/scripts/kallsyms" || { echo "EARLY_LINK_MISSING=$out/scripts/kallsyms" >&2; exit 71; }
  test -x "$kallsyms_src/scripts/mksysmap" || { echo "EARLY_LINK_MISSING=$kallsyms_src/scripts/mksysmap" >&2; exit 72; }

  k1=$(kallsyms_step 1)
  k2=$(kallsyms_step 2 "$k1")
  extra_kallsyms_obj=$k2
  last_kallsyms_syms="$ev/.tmp_vmlinux.kallsyms2.syms"

  size1=$(stat -c%s "$k1")
  size2=$(stat -c%s "$k2")
  if [[ "$size1" -ne "$size2" || -n "${KALLSYMS_EXTRA_PASS:-}" ]]; then
    k3=$(kallsyms_step 3 "$k2")
    extra_kallsyms_obj=$k3
    last_kallsyms_syms="$ev/.tmp_vmlinux.kallsyms3.syms"
  fi

  link_one "$ev/vmlinux.early" "$extra_kallsyms_obj" >"$ev/ld.stdout.txt" 2>"$ev/ld.stderr.txt"

  # Match Linux's final consistency gate: the final map must be the same map
  # that produced the final generated kallsyms object.
  NM="$NM" /bin/sh "$kallsyms_src/scripts/mksysmap" "$ev/vmlinux.early" "$ev/System.map.early" "$extra_kallsyms_obj" \
    >"$ev/final-mksysmap.stdout.txt" 2>"$ev/final-mksysmap.stderr.txt"
  if ! cmp -s "$ev/System.map.early" "$last_kallsyms_syms"; then
    echo 'EARLY_LINK_KALLSYMS_INCONSISTENT=1' >&2
    diff -u "$last_kallsyms_syms" "$ev/System.map.early" >"$ev/kallsyms-map.diff" || true
    exit 73
  fi
  echo 'EARLY_LINK_KALLSYMS_CONSISTENT=PASS' | tee "$ev/kallsyms-status.txt"
else
  link_one "$ev/vmlinux.early" >"$ev/ld.stdout.txt" 2>"$ev/ld.stderr.txt"
fi
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
test -s "$ev/vmlinux.early.map"
"$NM" -n "$ev/vmlinux.early" >"$ev/nm.txt"

if [[ "$with_kallsyms" == 1 ]] && is_enabled CONFIG_KALLSYMS; then
  # A weak-undefined kallsyms_offsets silently becomes address 0 and causes the
  # first BUG_ON in get_symbol_pos(). Do not ever certify such an image again.
  if is_enabled CONFIG_KALLSYMS_BASE_RELATIVE; then
    required_sym=kallsyms_offsets
  else
    required_sym=kallsyms_addresses
  fi
  sym_line=$(awk -v s="$required_sym" '$3 == s {print; exit}' "$ev/nm.txt")
  [[ -n "$sym_line" ]] || { echo "EARLY_LINK_KALLSYMS_SYMBOL_MISSING=$required_sym" >&2; exit 74; }
  sym_addr=$(awk '{print $1}' <<<"$sym_line")
  [[ "$sym_addr" != 0000000000000000 ]] || { echo "EARLY_LINK_KALLSYMS_SYMBOL_ZERO=$required_sym" >&2; exit 75; }
  grep -E " (kallsyms_(offsets|addresses|relative_base|num_syms))$" "$ev/nm.txt" >"$ev/kallsyms-symbols.txt" || true
  printf 'EARLY_LINK_KALLSYMS_SYMBOL=PASS symbol=%s address=%s\n' "$required_sym" "$sym_addr" | tee -a "$ev/kallsyms-status.txt"
fi

sha256sum "$ev/vmlinux.early" "$ev/Image.early" | tee "$ev/sha256.txt"
total_ms=$(((post_end-verify_start)/1000000))
echo "TIMING early_total_ms=$total_ms" | tee -a "$ev/timing.txt"
echo 'EARLY_LINK_V2=PASS'