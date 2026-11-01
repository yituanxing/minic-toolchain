#!/bin/sh
# Strict x86 PIE differential: hidden undefined weak symbols must use the GOT
# to preserve the absent-provider/null-address ELF contract. Strong hidden
# declarations in a different TU must still use their direct PC-relative form.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
if ! command -v readelf >/dev/null 2>&1; then
    echo 'X86_PIC_HIDDEN_WEAK=NOT_TESTED missing_tool=readelf'
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

weak_source="$root/tests/compiler/c0/gnu_weak_undefined_live.c"
weak_provider="$root/tests/compiler/c0/gnu_weak_undefined_provider.c"
strong_source="$root/tests/compiler/c0/elf_hidden_undefined_pic.c"
strong_provider="$root/tests/compiler/c0/elf_hidden_undefined_pic_owner.c"

"$host_cc" -E -P -std=gnu11 -x c "$weak_source" -o "$work/weak.i"
"$minic_cc" --target=x86_64-linux -fPIC -S "$work/weak.i" -o "$work/weak-mini.s"
"$minic_cc" --target=x86_64-linux -fPIE -S "$work/weak.i" -o "$work/weak-pie.s"
cmp "$work/weak-mini.s" "$work/weak-pie.s"

for symbol in hidden_missing_object hidden_missing_function; do
    grep -Fq "  movq $symbol@GOTPCREL(%rip), %rax" "$work/weak-mini.s" || {
        echo "X86_PIC_HIDDEN_WEAK=FAIL missing_GOT_symbol=$symbol" >&2
        exit 1
    }
    if grep -Fq "  leaq $symbol(%rip), %rax" "$work/weak-mini.s"; then
        echo "X86_PIC_HIDDEN_WEAK=FAIL direct_hidden_undefined_weak=$symbol" >&2
        exit 1
    fi
done

"$host_cc" -fPIC -c "$work/weak.i" -o "$work/weak-gnu.o"
"$host_cc" -fPIC -c "$work/weak-mini.s" -o "$work/weak-mini.o"
"$host_cc" -fPIC -c "$weak_provider" -o "$work/weak-provider.o"
readelf -Wr "$work/weak-mini.o" >"$work/weak-mini.reloc"
readelf -Wr "$work/weak-gnu.o" >"$work/weak-gnu.reloc"
readelf -Ws "$work/weak-mini.o" >"$work/weak-mini.sym"
for symbol in hidden_missing_object hidden_missing_function; do
    grep -E "R_X86_64_.*GOTPCREL.* $symbol - 4" "$work/weak-gnu.reloc" >/dev/null || {
        echo "X86_PIC_HIDDEN_WEAK=FAIL GNU_oracle_missing_GOT=$symbol" >&2
        exit 1
    }
    grep -E "R_X86_64_.*GOTPCREL.* $symbol - 4" "$work/weak-mini.reloc" >/dev/null || {
        echo "X86_PIC_HIDDEN_WEAK=FAIL missing_GOT_relocation=$symbol" >&2
        exit 1
    }
    grep -E "WEAK[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+$symbol$" \
        "$work/weak-mini.sym" >/dev/null || {
        echo "X86_PIC_HIDDEN_WEAK=FAIL missing_weak_hidden_binding=$symbol" >&2
        exit 1
    }
    if grep -E "R_X86_64_PC32.* $symbol - 4" "$work/weak-mini.reloc" >/dev/null; then
        echo "X86_PIC_HIDDEN_WEAK=FAIL forbidden_PC32=$symbol" >&2
        exit 1
    fi
done
for mode in gnu mini; do
    "$host_cc" -pie "$work/weak-$mode.o" "$work/weak-provider.o" -o "$work/weak-$mode"
    "$work/weak-$mode"
done

# A hidden, non-weak unresolved-in-this-TU symbol is not a null weak: it
# resolves to the strong provider in the linked unit and stays direct PCREL.
"$host_cc" -E -P -std=gnu11 -x c "$strong_source" -o "$work/strong.i"
"$minic_cc" --target=x86_64-linux -fPIC -S "$work/strong.i" -o "$work/strong-mini.s"
for symbol in hidden_data hidden_function; do
    grep -Fq "  leaq $symbol(%rip), %rax" "$work/strong-mini.s" || {
        echo "X86_PIC_HIDDEN_WEAK=FAIL strong_hidden_lost_PCREL=$symbol" >&2
        exit 1
    }
    if grep -Fq "$symbol@GOTPCREL(%rip)" "$work/strong-mini.s"; then
        echo "X86_PIC_HIDDEN_WEAK=FAIL strong_hidden_uses_GOT=$symbol" >&2
        exit 1
    fi
done
for symbol in default_data default_function; do
    grep -Fq "  movq $symbol@GOTPCREL(%rip), %rax" "$work/strong-mini.s"
done
"$host_cc" -fPIC -c "$work/strong.i" -o "$work/strong-gnu.o"
"$host_cc" -fPIC -c "$work/strong-mini.s" -o "$work/strong-mini.o"
"$host_cc" -fPIC -c "$strong_provider" -o "$work/strong-provider.o"
for mode in gnu mini; do
    "$host_cc" -pie "$work/strong-$mode.o" "$work/strong-provider.o" -o "$work/strong-$mode"
    "$work/strong-$mode"
done

echo 'X86_PIC_HIDDEN_WEAK=PASS GNU_MiniC_PIE=2x2 ELF_weak_hidden_GOT=2 strong_hidden_PCREL=2 default_GOT=2 missing_provider_null=1'
