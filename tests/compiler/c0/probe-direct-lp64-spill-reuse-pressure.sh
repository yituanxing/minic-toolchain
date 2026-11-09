#!/bin/sh
# The same Core per-block last-use spill allocator must serve AArch64,
# LoongArch64 and RV32/RV64. GNU performs all assembly/link/runtime steps;
# no MiniAS/MiniLD executable is part of this compiler oracle.
set -eu
: "${MINIC_CC:?point MINIC_CC at the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/direct-lp64-spill-reuse-pressure
mkdir -p "$work"
source="$root/tests/compiler/c0/direct_lp64_spill_reuse_pressure.c"
cc -std=gnu11 -E -P "$source" -o "$work/input.i"

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    grep -Fq 'lp64_spill_pressure:' "$work/$target.s"
    grep -Fq 'lp64_empty_values:' "$work/$target.s"
done
for target in aarch64 loongarch64; do
    # A branch into EXIT has no ordinary predecessor from the early-return
    # arm; preserve source-label roots while changing SSA slot lifetime.
    grep -Eq '^\.Luser_[0-9]+:' "$work/$target.s"
done

cc -std=gnu11 -O0 -fno-pie -no-pie "$source" -o "$work/gnu"
cc -fno-pie -no-pie -x assembler "$work/x86_64.s" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne 42 ]; then
    echo "DIRECT_LP64_SPILL_PRESSURE=FAIL gnu=$gnu_rc minic=$minic_rc" >&2
    exit 1
fi

if [ -n "${MINIC_PRECOMPACT_CC:-}" ]; then
    for target in aarch64 loongarch64; do
        "$MINIC_PRECOMPACT_CC" --target="$target-linux" -S \
            "$work/input.i" -o "$work/$target.precompact.s"
        before=$(wc -c <"$work/$target.precompact.s")
        after=$(wc -c <"$work/$target.s")
        if [ "$after" -ge "$before" ]; then
            echo "DIRECT_LP64_SPILL_PRESSURE=FAIL size_not_reduced target=$target before=$before after=$after" >&2
            exit 1
        fi
        echo "DIRECT_LP64_SPILL_SIZE=$target before_s_bytes=$before after_s_bytes=$after"
    done
fi

# Optional exact GNU external oracle for target-userland programs. Missing
# cross GCC/QEMU is reported as SKIP, never as runtime PASS.
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) cross=${AARCH64_GCC:-aarch64-linux-gnu-gcc}; qemu=${AARCH64_QEMU:-qemu-aarch64} ;;
        loongarch64) cross=${LOONGARCH64_GCC:-loongarch64-linux-gnu-gcc}; qemu=${LOONGARCH64_QEMU:-qemu-loongarch64} ;;
    esac
    if ! command -v "$cross" >/dev/null 2>&1 ||
       ! command -v "$qemu" >/dev/null 2>&1; then
        echo "DIRECT_LP64_SPILL_TARGET_RUNTIME=$target SKIP missing_cross_gcc_or_qemu"
        continue
    fi
    "$cross" -static -O0 "$source" -o "$work/$target.gnu"
    "$cross" -static -x assembler "$work/$target.s" -o "$work/$target.minic"
    set +e
    "$qemu" "$work/$target.gnu"; target_gnu_rc=$?
    "$qemu" "$work/$target.minic"; target_minic_rc=$?
    set -e
    if [ "$target_gnu_rc" -ne 42 ] || [ "$target_minic_rc" -ne 42 ]; then
        echo "DIRECT_LP64_SPILL_TARGET_RUNTIME=$target FAIL GNU=$target_gnu_rc MiniC=$target_minic_rc" >&2
        exit 1
    fi
    echo "DIRECT_LP64_SPILL_TARGET_RUNTIME=$target PASS GNU=$target_gnu_rc MiniC=$target_minic_rc"
done

printf 'DIRECT_LP64_SPILL_PRESSURE=PASS five_isa_S=5 x86_GNU_MiniC=%s/%s AArch64_s_bytes=%s LoongArch64_s_bytes=%s\n' \
    "$gnu_rc" "$minic_rc" "$(wc -c <"$work/aarch64.s")" \
    "$(wc -c <"$work/loongarch64.s")"
