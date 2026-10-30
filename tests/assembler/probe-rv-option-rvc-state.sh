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

# The legacy -march-omitted input must NOT silently accept GNU linker
# relaxation requests, which require R_RISCV_RELAX/ALIGN marker relocations.
# Unknown .option dialects must also fail rather than becoming no-ops.
for option in relax unknown_option 'arch, +zmadeup'; do
    printf '.text\n.option %s\ncall external_symbol\n' "$option" >"$work/unsupported-option.s"
    for args in "legacy" "rv64gc"; do
        options=()
        if [[ "$args" = rv64gc ]]; then options=(-march=rv64gc -mabi=lp64d); fi
        if "$MINIAS" "${options[@]}" -o "$work/invalid-option.o" \
                "$work/unsupported-option.s" >"$work/invalid.stdout" \
                2>"$work/invalid.stderr"; then
            echo "unsupported .option $option accepted ($args)" >&2
            exit 1
        fi
        test ! -e "$work/invalid-option.o"
        grep -q "unsupported-isa-option:$option" "$work/invalid.stderr"
    done
done

# Executable alignment is also a Target-site instruction decision.  A
# two-byte gap can be filled with C.NOP whenever the ISA supports C, even
# after .option norvc disabled new source-instruction compression. GNU GAS
# riscv_make_nops retains this distinction; ELF must advertise actual RVC.
cat >"$work/alignment-rvc.s" <<'EOF_RV'
.text
.option rvc
c.li a0,1
.option norelax
.p2align 3
lui a0,0x12345
EOF_RV
"$MINIAS" -march=rv64gc -mabi=lp64d \
    -o "$work/alignment-rvc.o" "$work/alignment-rvc.s"

cat >"$work/alignment-norvc.s" <<'EOF_RV'
.text
.option rvc
c.li a0,1
.option norvc
.option norelax
.p2align 3
lui a0,0x12345
EOF_RV
"$MINIAS" -march=rv64gc -mabi=lp64d \
    -o "$work/alignment-norvc.o" "$work/alignment-norvc.s"

# With no explicit -march, alignment itself may be the only C16 producer.
# A raw two-byte prefix is legal in legacy assembly; the resulting C.NOP
# must update ELF e_flags rather than relying on an earlier c.li.
cat >"$work/alignment-only.s" <<'EOF_RV'
.text
.byte 0,0
.option norelax
.p2align 3
lui a0,0x12345
EOF_RV
"$MINIAS" -mabi=lp64 -o "$work/alignment-only.o" \
    "$work/alignment-only.s"

cat >"$work/alignment-odd.s" <<'EOF_RV'
.text
.byte 0
.option norelax
.p2align 2
lui a0,0x12345
EOF_RV
"$MINIAS" -mabi=lp64 -o "$work/alignment-odd.o" \
    "$work/alignment-odd.s"

cat >"$work/alignment-gap2.s" <<'EOF_RV'
.text
.byte 0,0
.option norelax
.p2align 2
lui a0,0x12345
EOF_RV
"$MINIAS" -mabi=lp64 -o "$work/alignment-gap2.o" \
    "$work/alignment-gap2.s"

cat >"$work/alignment-gap4.s" <<'EOF_RV'
.text
.byte 0,0,0,0
.option norelax
.p2align 3
lui a0,0x12345
EOF_RV
"$MINIAS" -mabi=lp64 -o "$work/alignment-gap4.o" \
    "$work/alignment-gap4.s"

for mode in rvc norvc only; do
    python3 - "$work/alignment-$mode.o" <<'PY'
import re
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
assert len(payload) == 12 and payload[2:8] == b'\x01\x00\x13\x00\x00\x00', payload.hex()
PY
    readelf -h "$work/alignment-$mode.o" | grep -Eq 'Flags:.*RVC'
done
python3 - "$work/alignment-odd.o" <<'PY'
import re
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
assert len(payload) == 8 and payload[:4] == b'\x00\x00\x01\x00', payload.hex()
PY
readelf -h "$work/alignment-odd.o" | grep -Eq 'Flags:.*RVC'
python3 - "$work/alignment-gap2.o" "$work/alignment-gap4.o" <<'PY'
import re
import subprocess
import sys
def payload(path):
    dump = subprocess.check_output(['readelf', '-x', '.text', path], text=True)
    data = bytearray()
    for line in dump.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', line)
        if match:
            for cell in match.group(1).split():
                if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', cell):
                    break
                data.extend(bytes.fromhex(cell))
    return bytes(data)
assert payload(sys.argv[1])[:4] == b'\x00\x00\x01\x00'
assert payload(sys.argv[2])[:8] == b'\x00\x00\x00\x00\x13\x00\x00\x00'
PY
readelf -h "$work/alignment-gap2.o" | grep -Eq 'Flags:.*RVC'
if readelf -h "$work/alignment-gap4.o" | grep -Eq 'Flags:.*RVC'; then
    echo 'Four-byte-only NOP alignment falsely advertised RVC' >&2
    exit 1
fi

if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    for mode in rvc norvc odd gap2 gap4; do
        # GNU GAS pads the END of .text to sh_addralign by default. MiniAS
        # stores the logical section bytes without this optional tail pad;
        # suppress it so cmp checks the full instruction/alignment stream.
        riscv64-linux-gnu-as --no-pad-sections -march=rv64gc -mabi=lp64d \
            -o "$work/alignment-$mode.gnu.o" "$work/alignment-$mode.s"
        riscv64-linux-gnu-objcopy -O binary --only-section=.text \
            "$work/alignment-$mode.o" "$work/alignment-$mode.text"
        riscv64-linux-gnu-objcopy -O binary --only-section=.text \
            "$work/alignment-$mode.gnu.o" "$work/alignment-$mode.gnu.text"
        cmp "$work/alignment-$mode.text" "$work/alignment-$mode.gnu.text"
    done
fi

if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as --no-pad-sections -march=rv64gc -mabi=lp64d \
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

gnu_bytes=text_bytes(sys.argv[1])
mini_bytes=text_bytes(sys.argv[2])
if gnu_bytes != mini_bytes:
    print('GNU rv64gc text:    ',gnu_bytes.hex(' '),file=sys.stderr)
    print('MiniAS rv64gc text: ',mini_bytes.hex(' '),file=sys.stderr)
    raise AssertionError('RISC-V .option rvc restored bytes differ from GNU as')
PY
    echo 'MINIAS_RV_OPTION_RVC=PASS GNU_TEXT_EXACT'
else
    echo 'MINIAS_RV_OPTION_RVC=PASS GNU_NOT_AVAILABLE'
fi
echo 'MINIAS_RV_OPTION_RVC profiles=3 blocked=6 compressed_elf_flags=8 alignment=2,3,4,6'
