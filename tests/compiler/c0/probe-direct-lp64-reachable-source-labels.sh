#!/bin/sh
# Source-label roots must survive direct LP64 dead-block omission.
# Real GNU/QEMU GNU-AS semantic checks remain in the target jump-label suites.
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/direct-lp64-reachable-source-labels
mkdir -p "$work"
source="$root/tests/compiler/c0/direct_lp64_reachable_source_labels.c"
host="$root/tests/compiler/c0/direct_lp64_reachable_host.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -E -P -std=gnu11 -x c "$host" -o "$work/host.i"

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/host.i" -o "$work/$target.host.s"
    test -s "$work/$target.host.s"
done
for target in riscv32 riscv64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/source.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
for target in aarch64 loongarch64; do
    # LLVM/GNU-style source label aliases plus target-native block addresses
    # are both observable here, including labels reached by static relocation.
    test "$(grep -Ec '^\.Luser_[0-9]+:' "$work/$target.s")" -ge 5
    # Check *every* source relocation and every native block label target.
    # A Linux __jump_table site may be the only executable reference to its
    # patched destination; a missing definition must never pass -S-only.
    if [ "$target" = aarch64 ]; then prefix=arm; else prefix=loong; fi
    references=$(grep -Eo "\\.L(user_[0-9]+|mini_${prefix}_f[0-9]+_b[0-9]+)" \
        "$work/$target.s" | sort -u)
    for label in $references; do
        if ! grep -Fxq "$label:" "$work/$target.s"; then
            echo "DIRECT_LP64_SOURCE_LABEL_DIFF=FAIL missing_label=$label target=$target" >&2
            exit 1
        fi
    done
done

cc -O0 -fno-pie -no-pie -x c "$host" -o "$work/gnu"
cc -fno-pie -no-pie -x assembler "$work/x86_64.host.s" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne 42 ]; then
    echo "DIRECT_LP64_SOURCE_LABEL_DIFF=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi
printf 'DIRECT_LP64_SOURCE_LABEL_DIFF=PASS five_isa_S=5 GNU_x86_runtime=%s MiniC_x86_runtime=%s AArch64_s_bytes=%s LoongArch64_s_bytes=%s\n' \
  "$gnu_rc" "$minic_rc" "$(wc -c <"$work/aarch64.s")" "$(wc -c <"$work/loongarch64.s")"
