#!/usr/bin/env bash
# RISC-V .option rvc/norvc belongs to target-site state, not to Common.
# Under an explicit ISA it may restore C only when C was already selected;
# norelax is always safe because MiniAS does not emit linker RELAX markers.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/restored.s" <<'EOF_RV'
.text
.option norvc
addi a0, a0, 1
.option rvc
c.li a0, 3
.option push
.option norvc
addi a1, a1, 1
.option pop
c.li a2, 4
.option norelax
ret
EOF_RV

"$MINIAS" -march=rv64gc -mabi=lp64d \
    -o "$work/rv64gc.o" "$work/restored.s"
"$MINIAS" -march=rv32ec -mabi=ilp32e \
    -o "$work/rv32ec.o" "$work/restored.s"
"$MINIAS" -mabi=lp64 -o "$work/legacy.o" "$work/restored.s"

# Verify the compressed instructions were actually emitted after restoring
# the mode, without asking -march=rv64i to accept a C-ISA escape.
for abi in rv64gc rv32ec legacy; do
    text_hex=$(readelf -x .text "$work/$abi.o")
    if ! grep -qE '0305|1305|1346' <<<"$text_hex"; then
        echo "Missing expected machine words in $abi .text" >&2
        printf '%s\n' "$text_hex" >&2
        exit 1
    fi
    # No explicit -march must still advertise actually encoded C16 words.
    # GNU linkers rely on EF_RISCV_RVC; profile spelling alone is not proof.
    if ! readelf -h "$work/$abi.o" | grep -Eq 'Flags:.*RVC'; then
        echo "Missing EF_RISCV_RVC for emitted C16 words ($abi)" >&2
        exit 1
    fi
done

cat >"$work/forbidden.s" <<'EOF_RV'
.text
.option norvc
c.li a0, 1
EOF_RV
for arch in rv64gc rv32ec; do
    case "$arch" in
        rv64gc) abi=lp64d ;;
        rv32ec) abi=ilp32e ;;
    esac
    if "$MINIAS" "-march=$arch" "-mabi=$abi" \
            -o "$work/invalid.o" "$work/forbidden.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
        echo "Compressed instruction leaked through .option norvc ($arch)" >&2
        exit 1
    fi
    grep -q 'disabled-riscv-compressed' "$work/invalid.stderr"
done
if "$MINIAS" -mabi=lp64 -o "$work/invalid.o" "$work/forbidden.s" \
        >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'Legacy implicit ISA leaked a compressed instruction after norvc' >&2
    exit 1
fi
grep -q 'disabled-riscv-compressed' "$work/invalid.stderr"

cat >"$work/no-c.s" <<'EOF_RV'
.text
.option rvc
c.li a0, 1
EOF_RV
for arch in rv64i rv32i; do
    if "$MINIAS" "-march=$arch" \
            -o "$work/invalid.o" "$work/no-c.s" \
            >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
        echo ".option rvc escaped $arch strict ISA" >&2
        exit 1
    fi
    grep -q 'unsupported-isa-option:rvc' "$work/invalid.stderr"
done

cat >"$work/popped.s" <<'EOF_RV'
.text
.option norvc
.option push
.option rvc
c.li a0, 1
.option pop
c.li a0, 2
EOF_RV
if "$MINIAS" -march=rv64gc -mabi=lp64d \
        -o "$work/invalid.o" "$work/popped.s" \
        >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo '.option pop did not restore previous norvc mode' >&2
    exit 1
fi
grep -q 'disabled-riscv-compressed' "$work/invalid.stderr"

if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d \
        -o "$work/gnu.o" "$work/restored.s"
    # ELF producer metadata may differ; compare the emitted text words.
    python3 - "$work/gnu.o" "$work/rv64gc.o" <<'PY'
import re
import subprocess
import sys

def text_bytes(obj):
    dump = subprocess.check_output(['readelf', '-x', '.text', obj], text=True)
    payload = bytearray()
    for row in dump.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', row)
        if match:
            for cell in match.group(1).split():
                if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', cell):
                    break
                payload.extend(bytes.fromhex(cell))
    return bytes(payload)

assert text_bytes(sys.argv[1]) == text_bytes(sys.argv[2]), \
    'RISC-V .option rvc restored bytes differ from GNU as'
PY
    echo 'MINIAS_RV_OPTION_RVC=PASS GNU_TEXT_EXACT'
else
    echo 'MINIAS_RV_OPTION_RVC=PASS GNU_NOT_AVAILABLE'
fi
echo 'MINIAS_RV_OPTION_RVC profiles=3 blocked=6 compressed_elf_flags=3'
