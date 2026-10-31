#!/usr/bin/env bash
# Regression for LP64 use of the historical RV CoreObject stack-slot reuse.
# All preprocessing, assembly, linking and execution are GNU/QEMU; MiniC
# only performs C -> assembly for the tested target.
set -Eeuo pipefail
: "${MINIC_CC:?point MINIC_CC to the MiniC binary under test}"

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$repo/build/debug"}/tests/direct-lp64-source-object-slot-reuse
mkdir -p "$work"
source="$repo/tests/compiler/c0/direct_lp64_source_object_slot_reuse.c"

gcc -std=gnu11 -E -P "$source" -o "$work/input.i"
for target in x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/input.i" -o "$work/$target.s"
    for symbol in lp64_disjoint_local_slots \
                  lp64_escape_preserves_independent_slots \
                  lp64_crossblock_live_value; do
        grep -Fq "$symbol:" "$work/$target.s"
    done
done

# The temporary-local pressure must stay bounded on both LP64 backends.
# Runtime semantics below verify that lowering did not collapse live or
# escaping objects to the same storage. Before RV's CoreObject reuse was
# admitted into LP64 the exact fixture used 1584B on both targets; the safe
# pool reduces it to 80B. AArch uses movz/sub xN, LoongArch addi.d.
aarch_frame=$(awk '
    /^lp64_disjoint_local_slots:/ {inside=1; next}
    inside && /^\.Lmini_arm_/ {exit}
    inside && /movz x12, #[0-9]+/ {split($0, parts, "#"); frame=parts[2]}
    END {print frame + 0}
' "$work/aarch64.s")
loong_frame=$(awk '
    /^lp64_disjoint_local_slots:/ {inside=1; next}
    inside && /^\.Lmini_loong_/ {exit}
    inside && /addi\.d \$sp, \$sp, -[0-9]+/ {split($0, parts, "-"); frame=parts[2]}
    END {print frame + 0}
' "$work/loongarch64.s")
x86_frame=$(awk '
    /^lp64_disjoint_local_slots:/ {inside=1; next}
    inside && /^\.Lmini_x86_f[0-9]+_b0:/ {exit}
    inside && /subq \$[0-9]+, %rsp/ {split($0, parts, "\\$"); split(parts[2], size, ","); frame=size[1]}
    END {print frame + 0}
' "$work/x86_64.s")
if [ "$x86_frame" -le 0 ] || [ "$x86_frame" -gt 256 ] ||
   [ "$aarch_frame" -le 0 ] || [ "$aarch_frame" -gt 256 ] ||
   [ "$loong_frame" -le 0 ] || [ "$loong_frame" -gt 256 ]; then
    echo "LP64_SOURCE_OBJECT_FRAME=FAIL x86_frame=$x86_frame aarch64_frame=$aarch_frame loong_frame=$loong_frame" >&2
    exit 1
fi

gcc -std=gnu11 -O0 -fno-pie -no-pie "$source" -o "$work/x86.gnu"
gcc -fno-pie -no-pie -x assembler "$work/x86_64.s" -o "$work/x86.minic"
set +e
"$work/x86.gnu"; gnu_rc=$?
"$work/x86.minic"; minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne 42 ]; then
    echo "LP64_SOURCE_OBJECT_RUNTIME=x86_64_FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi

for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) cross=${AARCH64_GCC:-aarch64-linux-gnu-gcc}; qemu=${AARCH64_QEMU:-qemu-aarch64} ;;
        loongarch64) cross=${LOONGARCH64_GCC:-loongarch64-linux-gnu-gcc}; qemu=${LOONGARCH64_QEMU:-qemu-loongarch64} ;;
    esac
    if ! command -v "$cross" >/dev/null 2>&1 ||
       ! command -v "$qemu" >/dev/null 2>&1; then
        echo "LP64_SOURCE_OBJECT_RUNTIME=$target SKIP missing_cross_gcc_or_qemu"
        continue
    fi
    "$cross" -O0 -static "$source" -o "$work/$target.gnu"
    "$cross" -static -x assembler "$work/$target.s" -o "$work/$target.minic"
    set +e
    "$qemu" "$work/$target.gnu"; target_gnu_rc=$?
    "$qemu" "$work/$target.minic"; target_minic_rc=$?
    set -e
    if [ "$target_gnu_rc" -ne 42 ] || [ "$target_minic_rc" -ne 42 ]; then
        echo "LP64_SOURCE_OBJECT_RUNTIME=$target FAIL GNU=$target_gnu_rc MiniC=$target_minic_rc" >&2
        exit 1
    fi
    echo "LP64_SOURCE_OBJECT_RUNTIME=$target PASS GNU=$target_gnu_rc MiniC=$target_minic_rc"
done
echo "LP64_SOURCE_OBJECT_REUSE=PASS x86_GNU_MiniC=42/42 x86_frame=$x86_frame aarch64_frame=$aarch_frame loong_frame=$loong_frame targets_S=3"
