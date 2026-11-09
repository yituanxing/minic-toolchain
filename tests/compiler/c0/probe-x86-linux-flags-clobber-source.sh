#!/usr/bin/env bash
# GNU x86 accepts "flags" as a synonym for the machine condition-code
# clobber. Source recognition must not grant arbitrary machine-asm emission.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/flags.i" <<'EOF'
unsigned long flags_probe(unsigned long x) {
    asm volatile("nop" : "+r"(x) : : "flags");
    return x;
}
EOF
cc -std=gnu11 -fsyntax-only "$d/flags.i"
cc -std=gnu11 -O0 -S "$d/flags.i" -o "$d/flags-gnu.s"
# Target parser must recognize the token; machine must remain fail-closed
# until an exact opcode + physical register contract exists for this form.
if "$minic" --target=x86_64-linux -S "$d/flags.i" \
    -o "$d/flags-mini.s" >"$d/flags.out" 2>"$d/flags.err"; then
    echo "FAIL arbitrary GNU flags-clobbered asm admitted by x86 backend" >&2
    exit 1
fi
if grep -F 'unsupported GNU asm register clobber for target: flags' \
    "$d/flags.err" >/dev/null; then
    echo "FAIL x86 source-only flags clobber still rejected by parser" >&2
    exit 1
fi
sed 's/"flags"/"unknown_flag_name"/g' "$d/flags.i" >"$d/bad.i"
if "$minic" --target=x86_64-linux -S "$d/bad.i" \
    -o "$d/bad.s" >"$d/bad.out" 2>"$d/bad.err"; then
    echo "FAIL foreign GNU register clobber unexpectedly admitted" >&2
    exit 1
fi
grep -F 'unsupported GNU asm register clobber for target:' "$d/bad.err" >/dev/null
echo "MINIC_X86_LINUX_FLAGS_CLOBBER=PASS GCC_source=1 parser_alias=1 arbitrary_ASM_reject=1 invalid_reject=1"
