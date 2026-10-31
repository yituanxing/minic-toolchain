#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/riscv_packed_overflow_alignment.c"
${CC:-cc} -E -P -x c "$source_file" -o "$work/overflow.i"

# GNU native oracle: taking a packed member address is intentional here.
${CC:-cc} -std=gnu11 -O0 -Wall -Wextra -Werror \
    -Wno-address-of-packed-member "$source_file" -o "$work/reference"
"$work/reference"
"$MINIC_CC" --target=x86_64-linux -S "$work/overflow.i" -o "$work/x86_64-linux.s"
${CC:-cc} -no-pie "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/minic-native"
for target in riscv32-linux riscv64-linux aarch64-linux; do
    case "$target" in
        riscv32-linux) set -- -march=rv32i -mabi=ilp32 ;;
        riscv64-linux) set -- -march=rv64i -mabi=lp64 ;;
        *) set -- ;;
    esac
    "$MINIC_CC" --target="$target" "$@" -S "$work/overflow.i" \
        -o "$work/$target.s"
done
# LoongArch64 still rejects this checked-overflow Core opcode before
# target-specific memory transport.  Keep the capability gap observable as
# an exact negative, not a false claim of five-ISA checked-overflow support.
if "$MINIC_CC" --target=loongarch64-linux -S "$work/overflow.i" \
    -o "$work/loongarch64-linux.s" 2>"$work/loong.err"; then
    echo 'RISCV_PACKED_OVERFLOW=FAIL expected LoongArch checked-overflow rejection' >&2
    exit 1
fi
if ! grep -q 'LoongArch64: Core instruction/ABI not yet supported by M1 emitter' \
    "$work/loong.err"; then
    echo 'RISCV_PACKED_OVERFLOW=FAIL unexpected LoongArch compiler rejection' >&2
    exit 1
fi

# The overflow emitter has its own memory effects: every packed result
# requires byte-scatter, even if arithmetic uses a native or runtime libcall.
for target in riscv32-linux riscv64-linux; do
    for name in packed_overflow_narrow packed_overflow_add \
                packed_overflow_sub packed_overflow_mul packed_overflow_wide; do
        awk -v name="$name" '
            $0 == name ":" { inside=1; found=1; next }
            inside && index($0, ".size " name ",") == 1 { done=1; inside=0 }
            inside && $0 ~ /^  sb / { bytewise++ }
            inside && /\.Lminic_typed_.*_native/ { guard++ }
            END { if (!found || !done || bytewise == 0 || guard == 0) {
                printf "RISCV_PACKED_OVERFLOW=FAIL %s bytes=%d guards=%d\n", name, bytewise, guard > "/dev/stderr"
                exit 1
            }}
        ' "$work/$target.s"
    done
    # GNU local labels are emitted by different per-function helpers. Their
    # numeric suffix must be unique; otherwise the checked-overflow 1f/2f
    # branches may silently select a byte-transport helper's label.
    awk '
        /^\.Lminic_typed_.*_(native|done):$/ {
            name=$0
            sub(/:$/, "", name)
            if (seen[name]++) {
                print "RISCV_PACKED_OVERFLOW=FAIL duplicate assembly label " name > "/dev/stderr"
                exit 1
            }
            labels++
        }
        END { if (!labels) {
            print "RISCV_PACKED_OVERFLOW=FAIL missing typed transfer labels" > "/dev/stderr"
            exit 1
        }}
    ' "$work/$target.s"
done

echo 'RISCV_PACKED_OVERFLOW_ALIGNMENT=PASS GNU-native MiniC-x86 RV32/RV64/AArch -S guarded-bytewise Loong-expected-reject'
