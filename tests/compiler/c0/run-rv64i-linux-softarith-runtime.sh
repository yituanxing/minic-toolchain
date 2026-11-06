#!/usr/bin/env bash
# The real Linux 2064-owner RV64I compiler emits the historical libgcc ABI.
# Link its untouched generated object against strictly I-only upstream GNU /
# LLVM software arithmetic providers, as the RV32I Linux Kbuild path does.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
fixture="$root/tests/compiler/c0/riscv64i_freestanding_integer_ops.c"
builder="$root/tests/compiler/c0/build-rv64-gcc-runtime.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -std=gnu11 -O0 -Wall -Wextra "$fixture" -o "$work/host"
if "$work/host"; then rc=0; else rc=$?; fi
test "$rc" -eq 42 || {
    echo "RV64I_SOFTARITH=FAIL host_reference_rc=$rc expected=42" >&2
    exit 1
}
"$host_cc" -std=gnu11 -E -P -DRISCV_GUEST=1 -x c "$fixture" \
    -o "$work/original-gnu.i"
"$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/original-gnu.i" -o "$work/minic-rv64i.s"
test -s "$work/minic-rv64i.s"
for symbol in __muldi3 __udivdi3 __umoddi3 __divdi3 __moddi3 __multi3; do
    grep -Fq "call $symbol" "$work/minic-rv64i.s" || {
        echo "RV64I_SOFTARITH=FAIL historical_abi_missing=$symbol" >&2
        exit 2
    }
done

if [[ ${MINIC_RV64_SOFTARITH_SOURCE_ONLY:-0} == 1 ]]; then
    echo 'RV64I_SOFTARITH_SOURCE=PASS original_GNU_i=1 MiniC_S=1 hosted_libgcc_ABI=6 host_runtime=42'
    exit 0
fi

for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-as \
            riscv64-linux-gnu-ld riscv64-linux-gnu-ar \
            riscv64-linux-gnu-nm riscv64-linux-gnu-readelf \
            riscv64-linux-gnu-objdump qemu-riscv64; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "RV64I_SOFTARITH=FAIL missing_required_tool=$tool" >&2
        exit 3
    }
done
test -f "$builder" || {
    echo "RV64I_SOFTARITH=FAIL missing_upstream_builder=$builder" >&2
    exit 4
}
mkdir -p "$work/runtime"
if ! bash "$builder" "$work/runtime" >"$work/runtime-build.log" 2>&1; then
    echo 'RV64I_SOFTARITH=FAIL original_GNU_LLVM_runtime_build' >&2
    tail -n 70 "$work/runtime-build.log" >&2
    exit 5
fi
mapfile -t objects <"$work/runtime/rv64-runtime-objects.rsp"
test "${#objects[@]}" -ge 3 || {
    echo 'RV64I_SOFTARITH=FAIL empty_provider_object_set' >&2
    exit 6
}
for obj in "${objects[@]}"; do
    test -s "$obj"
    riscv64-linux-gnu-readelf -h "$obj" | grep -Eq 'Class:[[:space:]]+ELF64'
    riscv64-linux-gnu-readelf -h "$obj" | grep -Eq 'Machine:[[:space:]]+RISC-V'
    riscv64-linux-gnu-readelf -A "$obj" | grep -Eq 'Tag_RISCV_arch:.*rv64i'
done
# ELF e_flags must advertise LP64 soft-float with neither C nor floating
# register ABI: no 16-bit/compressed instructions can hide in this archive.
python3 - "${objects[@]}" <<'PY'
import pathlib, struct, sys
for name in sys.argv[1:]:
    data = pathlib.Path(name).read_bytes()
    if (data[:6] != b'\x7fELF\x02\x01' or
            struct.unpack_from('<H', data, 18)[0] != 243):
        raise SystemExit('RV64I_SOFTARITH=FAIL non_ELF64_little_RISCV ' + name)
    flags = struct.unpack_from('<I', data, 48)[0]
    if flags & 0x7:
        raise SystemExit(f'RV64I_SOFTARITH=FAIL ISA_or_LP64_ABI_flags {name} {flags:#x}')
PY
# Link with the *same archive* created by the established runtime-rv64i
# Makefile target. This must not turn into a second hand-maintained provider.
runtime_archive="$work/runtime/build/runtime/riscv64/rv64i-lp64/libminic-rt.a"
test -s "$runtime_archive"
riscv64-linux-gnu-nm -g --defined-only "$runtime_archive" > "$work/symbols.txt"
for symbol in __muldi3 __udivdi3 __umoddi3 __divdi3 __moddi3 __multi3; do
    grep -Eq "[[:space:]]${symbol}$" "$work/symbols.txt" || {
        echo "RV64I_SOFTARITH=FAIL missing_provider=$symbol" >&2
        exit 7
    }
done
# M arithmetic is forbidden even inside the external runtime archive.
riscv64-linux-gnu-objdump -d "$runtime_archive" > "$work/runtime.dis"
if grep -Eq '^[[:space:]]*[[:xdigit:]]+:[[:space:]]+[[:xdigit:]]+[[:space:]]+(mul|mulw|mulh|mulhu|mulhsu|div|divu|divw|divuw|rem|remu|remw|remuw)[[:space:]]' "$work/runtime.dis"; then
    echo 'RV64I_SOFTARITH=FAIL M_extension_instruction_in_runtime' >&2
    exit 8
fi

riscv64-linux-gnu-as -march=rv64i -mabi=lp64 \
    "$work/minic-rv64i.s" -o "$work/minic-rv64i.o"
riscv64-linux-gnu-ld -m elf64lriscv --no-relax -static -e _start \
    -o "$work/guest.elf" "$work/minic-rv64i.o" "$runtime_archive"
python3 - "$work/guest.elf" <<'PY'
import pathlib, struct, sys
data = pathlib.Path(sys.argv[1]).read_bytes()
if (data[:6] != b'\x7fELF\x02\x01' or
        struct.unpack_from('<H', data, 18)[0] != 243 or
        struct.unpack_from('<I', data, 48)[0] & 0x7):
    raise SystemExit('RV64I_SOFTARITH=FAIL linked_ELF64_RV64I_LP64_ABI')
PY
if riscv64-linux-gnu-nm -u "$work/guest.elf" | grep -q .; then
    echo 'RV64I_SOFTARITH=FAIL unresolved_GNU_link_symbols' >&2
    riscv64-linux-gnu-nm -u "$work/guest.elf" >&2
    exit 9
fi
if timeout 30s qemu-riscv64 "$work/guest.elf"; then rc=0; else rc=$?; fi
test "$rc" -eq 42 || {
    echo "RV64I_SOFTARITH=FAIL original_MiniC_GNU_LD_QEMU_exit=$rc expected=42" >&2
    exit 10
}
echo "RV64I_SOFTARITH_RUNTIME=PASS providers=GNU_libgcc_14.2.0,LLVM_compiler_rt_19.1.7 archive_objects=${#objects[@]} ISA=rv64i ABI=lp64 helper_ABI=6 GNU_LD=PASS QEMU=42 extra_linux_C_owners=0"
