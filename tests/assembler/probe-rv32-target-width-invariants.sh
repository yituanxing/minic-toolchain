#!/usr/bin/env bash
# XLEN is a target machine invariant, even without an explicit -march.
# RV32D has 64-bit FP registers; FLD/FSD are legal, but moving 64 bits into
# one GPR, FCVT.{L,LU}, and 64-bit A-extension AMOs are RV64 instructions.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

rv64_only=(
    'fmv.x.d a0,fa0'
    'fmv.d.x fa0,a0'
    'fcvt.d.l fa0,a0'
    'fcvt.d.lu fa0,a0'
    'fcvt.l.d a0,fa0'
    'fcvt.lu.d a0,fa0'
    'amoadd.d a0,a1,(a2)'
    'amoswap.d.aqrl a0,a1,(a2)'
    'lr.d.aq a0,(a2)'
    'sc.d.rl a0,a1,(a2)'
)
rv32_legal=(
    'fld fa0,0(a0)'
    'fsd fa0,0(a0)'
    'fcvt.d.w fa0,a0'
    'fcvt.w.d a0,fa0'
    'amoadd.w a0,a1,(a2)'
    'lr.w.aq a0,(a2)'
    'sc.w.rl a0,a1,(a2)'
)

count=0
for instruction in "${rv64_only[@]}"; do
    printf '.text\n%s\n' "$instruction" >"$work/in.s"
    # The instruction is invalid on RV32 independently of ISA selection:
    # old MiniAS incorrectly accepted most of these in both modes.
    for configuration in explicit implicit; do
        if [ "$configuration" = explicit ]; then
            opts=(--target=riscv -march=rv32g -mabi=ilp32d)
        else
            opts=(--target=riscv -mabi=ilp32)
        fi
        if "$MINIAS" "${opts[@]}" -o "$work/out.o" "$work/in.s" \
               >"$work/stdout" 2>"$work/stderr"; then
            printf 'RV32 accepted RV64 instruction: %s (%s)\n' \
                   "$instruction" "$configuration" >&2
            exit 1
        fi
        if ! grep -q 'riscv32-unsupported-opcode' "$work/stderr"; then
            printf 'Wrong XLEN rejection for %s (%s):\n' \
                   "$instruction" "$configuration" >&2
            sed -n '1,8p' "$work/stderr" >&2
            exit 1
        fi
        count=$((count + 1))
    done
done

for instruction in "${rv32_legal[@]}"; do
    printf '.text\n%s\n' "$instruction" >"$work/in.s"
    "$MINIAS" --target=riscv -march=rv32g -mabi=ilp32d \
        -o "$work/rv32.o" "$work/in.s"
    count=$((count + 1))
done

# Every RV64-only form remains legal on the corresponding RV64G ABI.
printf '.text\n' >"$work/rv64.s"
printf '%s\n' "${rv64_only[@]}" >>"$work/rv64.s"
"$MINIAS" --target=riscv -march=rv64g -mabi=lp64d \
    -o "$work/rv64.o" "$work/rv64.s"

if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64g -mabi=lp64d \
        -o "$work/gnu.o" "$work/rv64.s"
    python3 - "$work/rv64.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys

def read_text(path):
    rows = subprocess.check_output(['readelf', '-x', '.text', path], text=True)
    out = bytearray()
    for row in rows.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', row)
        if match:
            for cell in match.group(1).split():
                if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', cell):
                    break
                out.extend(bytes.fromhex(cell))
    return bytes(out)

mine, gnu = map(read_text, sys.argv[1:])
assert mine == gnu, f'RV64G encoding differs: mine={mine.hex()} GNU={gnu.hex()}'
PY
    echo 'MINIAS_RV32_XLEN_GUARD=PASS GNU_RV64_WORDS_EXACT'
else
    echo 'MINIAS_RV32_XLEN_GUARD=PASS GNU_RV64_WORDS_NOT_AVAILABLE'
fi
printf 'MINIAS_RV32_XLEN_GUARD cases=%s rv64_legal=%s\n' \
       "$count" "${#rv64_only[@]}"
