#!/bin/sh
# No Linux/QEMU required: check CLI rejection before tool dispatch and the
# existing RV64 CPP -> compiler -> MiniAS object path.
set -eu

: "${MINIC:?MINIC must name the standalone driver}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

printf 'int main(void) { return 3; }\n' >"$work/basic.i"
printf 'int dependency;\n' >"$work/dependency.h"

reject_preprocessed_option() {
    target=$1
    label=$2
    shift 2
    output="$work/$target-$label.s"
    if "$MINIC" --target="$target-linux" -S "$work/basic.i" \
            -o "$output" "$@" >"$work/reject.stdout" 2>"$work/reject.stderr"; then
        echo "FAIL accepted -S $label for $target" >&2
        exit 1
    fi
    if [ -e "$output" ] ||
       ! grep -Fq -- '-S-preprocessed-input-rejects-CPP-and-link-options' \
            "$work/reject.stderr"; then
        echo "FAIL unexpected -S $label failure for $target" >&2
        exit 1
    fi
}

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$MINIC" --target="$target-linux" -S "$work/basic.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    reject_preprocessed_option "$target" define -DMINIC_TEST=1
    reject_preprocessed_option "$target" undefine -UMINIC_TEST
    reject_preprocessed_option "$target" include-path "-I$work"
    reject_preprocessed_option "$target" system-path -isystem "$work"
    reject_preprocessed_option "$target" forced-include -include "$work/dependency.h"
    reject_preprocessed_option "$target" no-standard-include -nostdinc
    reject_preprocessed_option "$target" sysroot --sysroot "$work"
    reject_preprocessed_option "$target" freestanding -ffreestanding
    reject_preprocessed_option "$target" hosted -fhosted
    reject_preprocessed_option "$target" static -static
    reject_preprocessed_option "$target" no-stdlib -nostdlib
    reject_preprocessed_option "$target" no-startfiles -nostartfiles
    reject_preprocessed_option "$target" no-defaultlibs -nodefaultlibs
    if [ "$target" != riscv64 ]; then
        if "$MINIC" --target="$target-linux" -c "$work/basic.i" \
                -o "$work/$target.o" 2>"$work/foreign.err"; then
            echo "FAIL foreign staged target=$target" >&2
            exit 1
        fi
        grep -Fq 'target-requires-minic-cc-or-external-assembler' "$work/foreign.err"
        test ! -e "$work/$target.o"
    fi
    echo "PREPROCESSED_FLAGS=PASS target=$target CPP_and_link_only_rejected=13"
done

cat >"$work/default.c" <<'EOF'
#if !defined(__riscv) || !defined(__LP64__) || __riscv_xlen != 64
#error staged RV64 target predefines missing
#endif
#ifndef __riscv_float_abi_double
#error staged default RV64 must select lp64d
#endif
#ifndef __riscv_mul
#error legacy RV64GC has M
#endif
int staged_default(void) { return 7; }
EOF

cat >"$work/strict.c" <<'EOF'
#if !defined(__riscv) || !defined(__LP64__) || __riscv_xlen != 64
#error staged RV64I target predefines missing
#endif
#ifndef __riscv_float_abi_soft
#error staged RV64I must select lp64
#endif
#ifdef __riscv_mul
#error RV64I must not advertise M
#endif
int staged_strict(void) { return 9; }
EOF

"$MINIC" --target=riscv64-linux -c "$work/default.c" \
    -o "$work/default.o"
"$MINIC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -c "$work/strict.c" -o "$work/strict.o"
test -s "$work/default.o"
test -s "$work/strict.o"
if command -v readelf >/dev/null 2>&1; then
    readelf -h "$work/default.o" | grep -Fq 'RISC-V'
    readelf -h "$work/strict.o" | grep -Fq 'RISC-V'
fi

for invalid in -march=rv32i -march=rv64ima_zicsr_zifencei; do
    if "$MINIC" --target=riscv64-linux "$invalid" -c "$work/default.c" \
            -o "$work/invalid.o" 2>"$work/invalid.err"; then
        echo "FAIL accepted unqualified RV64 stage: $invalid" >&2
        exit 1
    fi
    test ! -e "$work/invalid.o"
done
if "$MINIC" --target=riscv64-linux -march=rv64i -mabi=lp64d -c \
        "$work/default.c" -o "$work/bad-abi.o" 2>"$work/bad-abi.err"; then
    echo 'FAIL accepted RV64I with LP64D' >&2
    exit 1
fi
grep -Fq 'incompatible-riscv-isa-abi' "$work/bad-abi.err"
test ! -e "$work/bad-abi.o"

echo 'STAGED_DRIVER_RECIPE=PASS rv64_default_lp64d rv64i_lp64 predefines=preserved foreign=fail_closed'
