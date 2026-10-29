#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?expected MiniC-cc executable}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

base="$repo/tests/compiler/c0"
src="$base/x86_linux_kvm_hypercall_full_abi.c"
cc -std=gnu11 -Wall -Wextra -Werror -O0 -S "$src" -o "$tmp/gnu.s"
cc -E -P -x c "$src" -o "$tmp/mini.i"
"$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
    -S "$tmp/mini.i" -o "$tmp/mini.s"
cc -c -x assembler "$tmp/gnu.s" -o "$tmp/gnu.o"
cc -c -x assembler "$tmp/mini.s" -o "$tmp/mini.o"

# All five variants retain two PC32 relocations to the instruction and
# replacement sections; the two direct AMD VMMCALLs emit no patch record.
for producer in gnu mini; do
    readelf -rW "$tmp/$producer.o" >"$tmp/$producer.reloc"
    test "$(awk '/\.rela\.altinstructions/ { in_alt=1; next } \
                /^Relocation section/ { in_alt=0 } \
                in_alt && /R_X86_64_PC32.*\.text \+/ { n++ } \
                END { print n+0 }' "$tmp/$producer.reloc")" -eq 5
    test "$(grep -Ec 'R_X86_64_PC32.*\.altinstr_replacement \+' "$tmp/$producer.reloc")" -eq 5
done
test "$(grep -Ec '^[[:space:]]*vmcall$' "$tmp/mini.s")" -eq 5
test "$(grep -Ec '^[[:space:]]*vmmcall$' "$tmp/mini.s")" -eq 7
grep -F 'movq ' "$tmp/mini.s" | grep -F '%rsi' >/dev/null
grep -F 'movq ' "$tmp/mini.s" | grep -F '%rdx' >/dev/null
test "$(grep -Fc 'pushq %rbx' "$tmp/mini.s")" -eq 7
test "$(grep -Fc 'popq %rbx' "$tmp/mini.s")" -eq 7

for negative in invalid_x86_linux_kvm_hypercall_abi invalid_x86_linux_kvm_hypercall_memory; do
    cc -std=gnu11 -Wall -Wextra -Werror -O0 -S "$base/$negative.c" \
        -o "$tmp/$negative.gnu.s"
    cc -E -P -x c "$base/$negative.c" -o "$tmp/$negative.i"
    if "$minic" --target=x86_64-linux -S "$tmp/$negative.i" \
        -o "$tmp/$negative.mini.s" >"$tmp/$negative.stdout" \
        2>"$tmp/$negative.stderr"; then
        echo "unsafe x86 KVM hypercall ABI admitted: $negative" >&2
        exit 1
    fi
    test ! -s "$tmp/$negative.mini.s"
done

for other in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    if "$minic" --target="$other" -S "$tmp/mini.i" \
        -o "$tmp/$other.s" >"$tmp/$other.stdout" 2>"$tmp/$other.stderr"; then
        echo "non-x86 target emitted x86 KVM privileged hypercall: $other" >&2
        exit 1
    fi
    test ! -s "$tmp/$other.s"
done

echo 'MINIC_X86_LINUX_KVM_HYPERCALL_ABI=PASS alt_arity=1..5 direct_arity=1,4 reloc_pc32=10 GNU_AS=2 reject_x86=2 reject_other_isa=4'
