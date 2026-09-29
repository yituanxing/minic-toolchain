#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <frozen-link-src> <linux-out> <evidence-dir> <full-linux-src>" >&2
  exit 64
fi

frozen_src=$(realpath "$1")
out=$(realpath "$2")
ev=$(realpath -m "$3")
full_src=$(realpath "$4")
mkdir -p "$ev"

cross=${CROSS_COMPILE:-riscv64-linux-gnu-}
CC=${cross}gcc
LD=${cross}ld
NM=${cross}nm
OBJCOPY=${cross}objcopy
OBJDUMP=${cross}objdump
READELF=${cross}readelf

# Keep the certified V2 construction as the base.  V2 intentionally omits
# generated kallsyms data; V3 adds only the normal kallsyms multi-pass phase.
bash "$(dirname "$0")/linux-early-runtime-link-v2.sh" "$frozen_src" "$out" "$ev"

if ! grep -q '^CONFIG_KALLSYMS=y' "$out/include/config/auto.conf"; then
  echo 'EARLY_KALLSYMS=SKIP config=disabled'
  echo 'EARLY_LINK_V3=PASS kallsyms=disabled'
  exit 0
fi

if [[ ! -x "$out/scripts/kallsyms" ]]; then
  make -C "$full_src" O="$out" ARCH=riscv CROSS_COMPILE="$cross" scripts/kallsyms >/dev/null
fi
test -x "$out/scripts/kallsyms"
test -x "$full_src/scripts/mksysmap"

ksymopt=()
grep -q '^CONFIG_KALLSYMS_ALL=y' "$out/include/config/auto.conf" && ksymopt+=(--all-symbols)
grep -q '^CONFIG_KALLSYMS_ABSOLUTE_PERCPU=y' "$out/include/config/auto.conf" && ksymopt+=(--absolute-percpu)
grep -q '^CONFIG_KALLSYMS_BASE_RELATIVE=y' "$out/include/config/auto.conf" && ksymopt+=(--base-relative)
grep -q '^CONFIG_LTO_CLANG=y' "$out/include/config/auto.conf" && ksymopt+=(--lto-clang)

link_with_kallsyms() {
  local output="$1" extra="${2:-}"
  (
    cd "$out"
    args=(
      -melf64lriscv -z noexecstack --no-warn-rwx-segments
      -z norelro --build-id=sha1 --orphan-handling=warn
      --script=arch/riscv/kernel/vmlinux.lds --strip-debug
      -o "$output"
      --whole-archive vmlinux.a init/version-timestamp.o --no-whole-archive
      --start-group ./drivers/firmware/efi/libstub/lib.a --end-group
    )
    [[ -n "$extra" ]] && args+=("$extra")
    "$LD" "${args[@]}"
  )
}

gen_kallsyms_obj() {
  local input="$1" tag="$2" exclude="${3:-}"
  local syms="$ev/.tmp_vmlinux.kallsyms${tag}.syms"
  local asm="$ev/.tmp_vmlinux.kallsyms${tag}.S"
  local obj="$ev/.tmp_vmlinux.kallsyms${tag}.o"
  if [[ -n "$exclude" ]]; then
    NM="$NM" /bin/sh "$full_src/scripts/mksysmap" "$input" "$syms" "$exclude" >/dev/null 2>&1
  else
    NM="$NM" /bin/sh "$full_src/scripts/mksysmap" "$input" "$syms" >/dev/null 2>&1
  fi
  "$out/scripts/kallsyms" "${ksymopt[@]}" "$syms" >"$asm"
  # The RISC-V kernel uses the LP64 soft-float ABI even when the toolchain's
  # user-space default is LP64D.  Kallsyms output is data-only assembly, so
  # preserving the kernel ABI here is sufficient to avoid an ELF float-ABI
  # mismatch without guessing extra ISA extensions.
  "$CC" -mabi=lp64 -D__ASSEMBLY__ \
    -I"$full_src/arch/riscv/include" \
    -I"$out/arch/riscv/include/generated" \
    -I"$full_src/arch/riscv/include/uapi" \
    -I"$out/arch/riscv/include/generated/uapi" \
    -I"$full_src/include" \
    -I"$out/include" \
    -I"$full_src/include/uapi" \
    -I"$out/include/generated/uapi" \
    -c -o "$obj" "$asm"
  test -s "$obj"
  "$READELF" -h "$obj" | grep -q 'soft-float ABI'
  echo "KALLSYMS_OBJ_ABI=PASS tag=$tag abi=lp64" >&2
  printf '%s\n' "$obj"
}

start=$(date +%s%N)
cp "$ev/vmlinux.early" "$ev/vmlinux.nokallsyms"

# Linux's normal link-vmlinux.sh performs at least two kallsyms passes.  A
# third pass is needed only if the generated object size changes.
k1=$(gen_kallsyms_obj "$ev/vmlinux.nokallsyms" 1)
link_with_kallsyms "$ev/.tmp_vmlinux.kallsyms2" "$k1"
k2=$(gen_kallsyms_obj "$ev/.tmp_vmlinux.kallsyms2" 2 "$k1")

s1=$(wc -c <"$k1")
s2=$(wc -c <"$k2")
final_k="$k2"
passes=2
if [[ "$s1" -ne "$s2" ]]; then
  link_with_kallsyms "$ev/.tmp_vmlinux.kallsyms3" "$k2"
  k3=$(gen_kallsyms_obj "$ev/.tmp_vmlinux.kallsyms3" 3 "$k2")
  final_k="$k3"
  passes=3
fi

link_with_kallsyms "$ev/vmlinux.early" "$final_k"

if grep -q '^CONFIG_RELOCATABLE=y' "$out/include/config/auto.conf"; then
  /bin/sh "$frozen_src/arch/riscv/tools/relocs_check.sh" "$OBJDUMP" "$NM" "$ev/vmlinux.early"
  "$OBJCOPY" \
    --remove-section='.rel.*' --remove-section='.rel__*' \
    --remove-section='.rela.*' --remove-section='.rela__*' \
    "$ev/vmlinux.early"
fi
"$OBJCOPY" -O binary -R .note -R .note.gnu.build-id -R .comment -S \
  "$ev/vmlinux.early" "$ev/Image.early"
"$NM" -n "$ev/vmlinux.early" >"$ev/nm.txt"

for sym in kallsyms_offsets kallsyms_num_syms kallsyms_relative_base; do
  line=$(grep -E "[[:space:]]${sym}$" "$ev/nm.txt" | head -n1 || true)
  [[ -n "$line" ]] || { echo "EARLY_KALLSYMS_MISSING=$sym" >&2; exit 71; }
  addr=$(awk '{print $1}' <<<"$line")
  [[ "$addr" != 0000000000000000 ]] || { echo "EARLY_KALLSYMS_ZERO=$sym" >&2; exit 72; }
  echo "EARLY_KALLSYMS_SYMBOL $line"
done

end=$(date +%s%N)
echo "TIMING early_kallsyms_ms=$(((end-start)/1000000))" | tee -a "$ev/timing.txt"
echo "EARLY_KALLSYMS=PASS passes=$passes object_size=$(wc -c <"$final_k")"
echo 'EARLY_LINK_V3=PASS kallsyms=linked'
