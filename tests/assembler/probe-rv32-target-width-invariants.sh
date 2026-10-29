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

# Zbb rev8 has two ratified encodings: RV32 uses imm12=0x698,
# RV64 uses imm12=0x6b8. Both forms have the same mnemonic, so XLEN is a
# Target encoding decision rather than a source-spelling classification.
printf '.text\nrev8 a0,a1\n' >"$work/rev8.s"
for arch in rv32i_zbb rv64i_zbb; do
    case "$arch" in
        rv32i_zbb) abi=ilp32 ; expected=0x6985d513 ;;
        rv64i_zbb) abi=lp64 ; expected=0x6b85d513 ;;
    esac
    "$MINIAS" "-march=$arch" "-mabi=$abi" \
        -o "$work/$arch.o" "$work/rev8.s"
    python3 - "$work/$arch.o" "$expected" <<'PY'
import re
import struct
import subprocess
import sys
dump = subprocess.check_output(['readelf', '-x', '.text', sys.argv[1]], text=True)
payload = bytearray()
for line in dump.splitlines():
    match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', line)
    if match:
        for cell in match.group(1).split():
            if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', cell):
                break
            payload.extend(bytes.fromhex(cell))
expected = struct.pack('<I', int(sys.argv[2], 0))
assert payload == expected, (payload.hex(), expected.hex())
PY
    if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
       command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
        riscv64-linux-gnu-as "-march=$arch" "-mabi=$abi" \
            -o "$work/$arch.gnu.o" "$work/rev8.s"
        riscv64-linux-gnu-objcopy -O binary --only-section=.text \
            "$work/$arch.o" "$work/$arch.text"
        riscv64-linux-gnu-objcopy -O binary --only-section=.text \
            "$work/$arch.gnu.o" "$work/$arch.gnu.text"
        cmp "$work/$arch.text" "$work/$arch.gnu.text"
    fi
done
if "$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/rev8-invalid.o" \
    "$work/rev8.s" >"$work/rev8.out" 2>"$work/rev8.err"; then
    echo 'RV32I without Zbb accepted rev8' >&2
    exit 1
fi
grep -Eq 'unsupported-isa-feature|rv32i-unsupported-machine-encoding' \
    "$work/rev8.err"

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
printf 'MINIAS_RV32_XLEN_GUARD cases=%s rv64_legal=%s rev8_xlen=2 rev8_no_zbb_rejected=1\n' \
       "$count" "${#rv64_only[@]}"
