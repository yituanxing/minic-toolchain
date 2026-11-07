#!/usr/bin/env bash
# Original Linux 6.6 arch/x86/lib/iomem.c GNU early-clobber/tied SI/DI/CX.
# Run MiniC source admission, typed MachineIR GNU AS and real execution.
set -Eeuo pipefail
minic=$1
test -x "$minic"
root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
source_file="$root/tests/target/x86_64_linux_iomem_copy_tied.c"
cc -E -P -std=gnu11 -x c "$source_file" -o "$tmp/genuine.i"
"$minic" --target=x86_64-linux -S "$tmp/genuine.i" -o "$tmp/minic.s"
as --64 -o "$tmp/minic.o" "$tmp/minic.s"
cc -no-pie "$tmp/minic.o" -o "$tmp/minic"
cc -std=gnu11 -O2 -no-pie "$source_file" -o "$tmp/gcc"
"$tmp/minic"
"$tmp/gcc"
for insn in 'rep; movsl' 'testb $2, %al' 'movsw' 'movsb'; do
    grep -Fq "$insn" "$tmp/minic.s"
done
# Source text or constraints cannot acquire privileged side effects by
# suffixing an exact original template or spoofing a tied output register.
sed 's/"movsb" :/"movsb; cli" :/' "$source_file" > "$tmp/extra.c"
sed 's/"=&D"(end_dest)/"=&a"(end_dest)/' "$source_file" > "$tmp/binding.c"
sed 's/"movsw" :/"movsw; cli" :/' "$source_file" > "$tmp/word.c"
for case in extra binding word; do
    cc -E -P -std=gnu11 -x c "$tmp/$case.c" -o "$tmp/$case.i"
    if "$minic" --target=x86_64-linux -S "$tmp/$case.i" \
            -o "$tmp/$case.s" >/dev/null 2>&1; then
        echo "FAIL unsafe Linux IOMEM form: $case" >&2
        exit 1
    fi
    test ! -e "$tmp/$case.s"
done
echo 'MINIC_X86_LINUX_IOMEM_COPY=PASS forms=3 GNU_AS=PASS GNU_RUN=PASS MiniC_RUN=PASS lengths=0..31 negative=3'
