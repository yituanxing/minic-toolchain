#!/usr/bin/env bash
# GNU objcopy -I binary -O elf64-loongarch generates data-only objects with
# e_flags=0; these must not poison the ABI supplied by actual code objects.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
GNU_LD=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/code.S" <<'ASM'
.text
.globl _start
_start:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
ASM
cat >"$work/blob.S" <<'ASM'
.section .rodata,"a",@progbits
.globl data_blob
data_blob:
    .quad 0x1020304050607080
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/code.o" "$work/code.S"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/data.o" "$work/blob.S"
python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys
p=Path(sys.argv[1])
for source,name,flags in (('data.o','blob-neutral.o',0),
                          ('code.o','code-invalid.o',0),
                          ('data.o','blob-invalid.o',0x80),
                          ('data.o','blob-abi42.o',0x42)):
    b=bytearray((p/source).read_bytes())
    assert struct.unpack_from('<I',b,48)[0] == 0x43
    struct.pack_into('<I',b,48,flags)
    (p/name).write_bytes(b)
PY
for order in early late; do
    if [[ $order == early ]]; then
        inputs=("$work/blob-neutral.o" "$work/code.o")
    else
        inputs=("$work/code.o" "$work/blob-neutral.o")
    fi
    "$MINILD" -m elf64loongarch -static -e _start \
        -o "$work/$order.mini.elf" "${inputs[@]}"
    readelf -Wh "$work/$order.mini.elf" | \
        grep -Eq 'Flags:.*0x43,.*DOUBLE-FLOAT'
done
for item in code-invalid.o blob-invalid.o; do
    if "$MINILD" -m elf64loongarch -static -e _start \
        -o "$work/should-not-exist.elf" "$work/$item" "$work/code.o" \
        >"$work/$item.stdout" 2>"$work/$item.stderr"; then
        echo "MINILD_LOONG_NEUTRAL_FLAGS=FAIL accepted=$item" >&2
        exit 1
    fi
    grep -Fq unsupported-target-elf-flags "$work/$item.stderr"
done
if "$MINILD" -m elf64loongarch -static -e _start \
        -o "$work/should-not-exist.elf" \
        "$work/code.o" "$work/blob-abi42.o" \
        >"$work/mixed.stdout" 2>"$work/mixed.stderr"; then
    echo 'MINILD_LOONG_NEUTRAL_FLAGS=FAIL mixed_ABI_accepted' >&2
    exit 1
fi
grep -Fq incompatible-target-elf-flags "$work/mixed.stderr"
echo 'MINILD_LOONG_NEUTRAL_FLAGS=PASS neutral_blob_both_orders=true ABI=0x43 code0_rejected=true invalid_data_rejected=true mixed_ABI_rejected=true'

if ! command -v "$GNU_LD" >/dev/null 2>&1; then
    if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
        echo "MINILD_LOONG_NEUTRAL_FLAGS=FAIL missing_GNU_oracle=$GNU_LD" >&2
        exit 1
    fi
    echo 'MINILD_LOONG_NEUTRAL_FLAGS=SKIP GNU_oracle_missing'
    exit 0
fi
for order in early late; do
    if [[ $order == early ]]; then
        inputs=("$work/blob-neutral.o" "$work/code.o")
    else
        inputs=("$work/code.o" "$work/blob-neutral.o")
    fi
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/$order.gnu.elf" "${inputs[@]}"
    gnu_flag=$(readelf -Wh "$work/$order.gnu.elf" | sed -n 's/.*Flags:[[:space:]]*\(0x[0-9a-f]*\).*/\1/p')
    mini_flag=$(readelf -Wh "$work/$order.mini.elf" | sed -n 's/.*Flags:[[:space:]]*\(0x[0-9a-f]*\).*/\1/p')
    [[ $gnu_flag == 0x43 && $mini_flag == "$gnu_flag" ]]
done
echo 'MINILD_LOONG_NEUTRAL_FLAGS=PASS GNU_BFD_early_late_e_flags_equal=true'
