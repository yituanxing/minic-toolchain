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
    if ! "$MINIC_CC" --target="$target" "$@" -S "$work/overflow.i" \
            -o "$work/$target.s" 2>"$work/$target.err"; then
        echo "RISCV_PACKED_OVERFLOW_STAGE=compile_failed target=$target" >&2
        tail -n 40 "$work/$target.err" >&2 || true
        if [ "$target" = riscv32-linux ]; then
            # Diagnosis only: compile each original checked-overflow function
            # independently with the same packed type and target options.
            # This keeps the complete original translation-unit gate intact.
            for name in packed_overflow_narrow packed_overflow_add \
                        packed_overflow_sub packed_overflow_mul packed_overflow_wide; do
                awk -v want="$name" '
                    index($0, "int packed_overflow_narrow(") == 1 { functions=1 }
                    !functions { print; next }
                    index($0, "int " want "(") == 1 { body=1 }
                    body { print; if (/^}/) exit }
                ' "$work/overflow.i" > "$work/$name.i"
                if "$MINIC_CC" --target="$target" "$@" \
                        -S "$work/$name.i" -o "$work/$name.s" \
                        2>"$work/$name.err"; then
                    echo "RISCV_PACKED_OVERFLOW_STAGE=function_pass name=$name" >&2
                else
                    echo "RISCV_PACKED_OVERFLOW_STAGE=function_failed name=$name" >&2
                    tail -n 20 "$work/$name.err" >&2 || true
                fi
            done
        fi
        exit 1
    fi
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

# The RV32/RV64 MachineIR selector emits typed SB for every individual
# byte of the checked-overflow result, including a packed field at odd
# offset.  The removed pre-MachineIR direct emitter used textual
# .Lminic_typed_*_native labels; they are NOT a MachineIR correctness
# obligation. Check the actual byte-count contract instead (2/4/8 bytes)
# and keep the GNU native + MiniC x86 differential above unchanged.
for target in riscv32-linux riscv64-linux; do
    for spec in packed_overflow_narrow:2 packed_overflow_add:4 \
                packed_overflow_sub:4 packed_overflow_mul:4 \
                packed_overflow_wide:8; do
        name=${spec%:*}
        required=${spec#*:}
        awk -v name="$name" -v required="$required" '
            $0 == name ":" { inside=1; found=1; next }
            inside && index($0, ".size " name ",") == 1 {
                done=1
                inside=0
            }
            inside && $0 ~ /^  sb / { bytewise++ }
            END {
                if (!found || !done || bytewise < required) {
                    printf "RISCV_PACKED_OVERFLOW=FAIL %s bytes=%d required=%d\\n",
                           name, bytewise, required > "/dev/stderr"
                    exit 1
                }
            }
        ' "$work/$target.s"
    done
done

echo 'RISCV_PACKED_OVERFLOW_ALIGNMENT=PASS GNU-native MiniC-x86 RV32/RV64/AArch -S guarded-bytewise Loong-expected-reject'
