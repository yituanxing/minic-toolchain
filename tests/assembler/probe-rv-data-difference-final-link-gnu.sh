#!/usr/bin/env bash
# Link-time semantic oracle for PC-relative .word expressions in RISC-V .text.
#
# The CALL is independently marked R_RISCV_RELAX before both data fields.
# Its 8 -> 4 byte linker shrink changes the value of `. - symbol` and
# `symbol - .`, even though the two names are in one assembled input section.
# A final `.option norelax` is deliberate: it must not erase the already
# emitted call site's linker-motion contract.  Compare the GNU baseline and
# MiniAS independently; GNU object bytes need not be identical.
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact MiniAS binary}"
gas=${RISCV_GAS:-riscv64-linux-gnu-as}
linker=${RISCV_LD:-riscv64-linux-gnu-ld}
copy=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
for program in "$gas" "$linker" "$copy" readelf python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_RV_DATA_DIFF_FINAL_LINK=BLOCKED missing=$program" >&2
            exit 2
        fi
        echo "MINIAS_RV_DATA_DIFF_FINAL_LINK=SKIP missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/difference.s" <<'ASM'
.option norelax
.text
.globl diff_start
diff_start:
.option relax
    call external_target
.globl diff_dot_minus
diff_dot_minus:
    .4byte .-diff_start
.globl diff_symbol_minus
diff_symbol_minus:
    .4byte diff_start-.
.option norelax
.globl diff_end
diff_end:
    ret
ASM

cat >"$work/definition.s" <<'ASM'
.text
.globl external_target
external_target:
    ret
ASM

for bits in 32 64; do
    if [[ $bits == 32 ]]; then
        mabi=ilp32
        emulation=elf32lriscv
    else
        mabi=lp64
        emulation=elf64lriscv
    fi
    march="rv${bits}i2p1"
    "$gas" --no-pad-sections "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$work/$bits-definition.o" "$work/definition.s"
    "$gas" --no-pad-sections "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$work/$bits-gnu.o" "$work/difference.s"
    "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
        -o "$work/$bits-mini.o" "$work/difference.s"
    for producer in gnu mini; do
        readelf -rW "$work/$bits-$producer.o" >"$work/$bits-$producer.rela"
        for mode in no-relax relax; do
            stem="$bits-$producer-$mode"
            linked="$work/$stem.elf"
            "$linker" -m "$emulation" "--$mode" \
                -e diff_start -o "$linked" \
                "$work/$bits-$producer.o" "$work/$bits-definition.o"
            "$copy" --dump-section ".text=$work/$stem.text.bin" "$linked"
        done
    done
done

python3 - "$work" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1])
errors = []
gnu_errors = []

def symbol(elf, name):
    for line in subprocess.check_output(["readelf", "-Ws", str(elf)], text=True).splitlines():
        columns = line.split()
        if len(columns) >= 8 and columns[0].endswith(":") and columns[-1] == name:
            return int(columns[1], 16)
    raise RuntimeError(f"missing linked symbol {name}: {elf}")

def section_vma(elf, name):
    lines = subprocess.check_output(["readelf", "-SW", str(elf)], text=True)
    regex = re.compile(r"^\s*\[\s*\d+\]\s+" + re.escape(name) +
                       r"\s+\S+\s+([0-9a-fA-F]+)\b")
    for line in lines.splitlines():
        match = regex.match(line)
        if match:
            return int(match.group(1), 16)
    raise RuntimeError(f"missing output section {name}: {elf}")

for bits in (32, 64):
    for producer in ("gnu", "mini"):
        observed = {}
        for mode in ("no-relax", "relax"):
            stem = f"{bits}-{producer}-{mode}"
            elf = root / f"{stem}.elf"
            raw = (root / f"{stem}.text.bin").read_bytes()
            vma = section_vma(elf, ".text")
            start = symbol(elf, "diff_start")
            addresses = {name: symbol(elf, name) for name in
                         ("diff_dot_minus", "diff_symbol_minus", "diff_end")}
            observed[mode] = (start, addresses)
            for name, expected_signed in (
                ("diff_dot_minus", addresses["diff_dot_minus"] - start),
                ("diff_symbol_minus", start - addresses["diff_symbol_minus"]),
            ):
                offset = addresses[name] - vma
                if offset < 0 or offset + 4 > len(raw):
                    raise RuntimeError(f"bad data-field offset {offset}: {elf}")
                actual = int.from_bytes(raw[offset:offset + 4], "little")
                expected = expected_signed & 0xFFFFFFFF
                correct = actual == expected
                print("MINIAS_RV_DATA_DIFF_FINAL_LINK=CASE "
                      f"rv={bits} producer={producer} mode={mode} "
                      f"field={name} expected=0x{expected:08x} "
                      f"actual=0x{actual:08x} "
                      f"semantic={'OK' if correct else 'BAD'}")
                if not correct:
                    (gnu_errors if producer == "gnu" else errors).append(f"{stem}:{name}")
        before = observed["no-relax"][1]["diff_dot_minus"] - observed["no-relax"][0]
        after = observed["relax"][1]["diff_dot_minus"] - observed["relax"][0]
        if before - after != 4:
            errors.append(f"{bits}:{producer}:no-real-call-shrink:{before - after}")

if gnu_errors:
    print("MINIAS_RV_DATA_DIFF_FINAL_LINK=GNU_BASELINE_UNSAFE " + ",".join(gnu_errors))
if errors:
    print("MINIAS_RV_DATA_DIFF_FINAL_LINK=FAIL " + ",".join(errors), file=sys.stderr)
    sys.exit(1)
print("MINIAS_RV_DATA_DIFF_FINAL_LINK=PASS rv32_rv64=2 "
      "producers=2 linker_modes=2 fields=2 mini_final_semantics=correct")
PY
