#!/usr/bin/env bash
# One target-neutral CLI/Invocation option reaches each Target's initial ISA
# state before the first SourceGraph snapshot. Local .option scopes may then
# override it without mutating TargetSpec or the object-format ABI.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/riscv.s" <<'ASM'
.text
    call remote
    .p2align 4
    ret
ASM
cat >"$work/loongarch64.s" <<'ASM'
.text
    la.local $a0, near
    .p2align 4
near:
    nop
ASM

for arch in rv32 rv64 loongarch64; do
    for mode in default relax no-relax; do
        args=()
        [[ $mode == default ]] || args+=("-m$mode")
        if [[ $arch == loongarch64 ]]; then
            "$MINIAS" --target=loongarch64 "${args[@]}" \
                -o "$work/$arch.$mode.o" "$work/loongarch64.s"
        else
            bits=${arch#rv}
            if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
            "$MINIAS" --target=riscv -march="rv${bits}i" -mabi="$abi" \
                "${args[@]}" -o "$work/$arch.$mode.o" "$work/riscv.s"
        fi
    done
done

for arch in rv32 rv64; do
    readelf -Wr "$work/$arch.relax.o" | grep -q R_RISCV_RELAX
    readelf -Wr "$work/$arch.relax.o" | grep -q R_RISCV_ALIGN
    ! readelf -Wr "$work/$arch.no-relax.o" | grep -q R_RISCV_RELAX
    ! readelf -Wr "$work/$arch.no-relax.o" | grep -q R_RISCV_ALIGN
    ! readelf -Wr "$work/$arch.default.o" | grep -q R_RISCV_RELAX
done

readelf -Wr "$work/loongarch64.relax.o" | grep -q R_LARCH_RELAX
readelf -Wr "$work/loongarch64.relax.o" | grep -q R_LARCH_ALIGN
readelf -Wr "$work/loongarch64.default.o" | grep -q R_LARCH_ALIGN
! readelf -Wr "$work/loongarch64.no-relax.o" | grep -q R_LARCH_RELAX
! readelf -Wr "$work/loongarch64.no-relax.o" | grep -q R_LARCH_ALIGN

for arch in x86_64 aarch64; do
    if "$MINIAS" --target="$arch" -mrelax -o "$work/rejected.o" \
       "$work/riscv.s" >"$work/$arch.stdout" 2>"$work/$arch.stderr"; then
        echo "MINIAS_LINK_RELAX_CLI=FAIL accepted unsupported target=$arch" >&2
        exit 1
    fi
    grep -q 'unsupported-target-link-relax-option' "$work/$arch.stderr"
done
echo 'MINIAS_LINK_RELAX_CLI=PASS RV32_RV64=2 LoongArch=1 modes=3 unsupported_targets=2'
