#!/usr/bin/env bash
# One x86 Target form registry must drive reg/reg and reg/memory encoding.
# GNU GAS is the independent byte and relocation oracle; bad forms reject.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy objdump readelf cmp python3; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/forms.s" <<'ASM'
.text
.globl x86_modrm_forms
x86_modrm_forms:
    btw %ax,%bx
    btl %eax,%ebx
    btq %r8,%r9
    btsw %cx,%dx
    btsl %ecx,%edx
    btsq %r10,%r11
    btrw %ax,%bx
    btrl %eax,%ebx
    btrq %r8,%r9
    btcw %cx,%dx
    btcl %ecx,%edx
    btcq %r10,%r11
    btq %r8,external(%rip)
    btsw %ax,8(%r13)
    btrl %ecx,external(%rip)
    btcq %r10,16(%r14)

    xaddb %al,%bl
    xaddb %ah,%bl
    xaddb %r8b,%r9b
    xaddb %spl,%al
    xaddw %ax,%bx
    xaddl %eax,%ebx
    xaddq %r8,%r9
    xaddb %al,(%r10)
    xaddw %r8w,8(%r13)
    xaddq %r12,external(%rip)
    xadd %eax,%ebx

    cmpxchgb %al,%bl
    cmpxchgb %ah,%bl
    cmpxchgb %r8b,%r9b
    cmpxchgb %spl,%al
    cmpxchgw %ax,%bx
    cmpxchgl %eax,%ebx
    cmpxchgq %r8,%r9
    cmpxchgb %al,(%r10)
    cmpxchgw %r8w,8(%r13)
    cmpxchgq %r12,external(%rip)
    cmpxchg %eax,%ebx

    xchgb %al,%bl
    xchgb %ah,%bl
    xchgb %ah,%al
    xchgb %spl,%al
    xchgb %r8b,%r9b
    xchgw %ax,%bx
    xchgw %bx,%ax
    xchgw %ax,%r8w
    xchgl %eax,%ebx
    xchgl %eax,%eax
    xchgl %ecx,%ebx
    xchgq %rax,%rbx
    xchgq %r8,%rax
    xchgq %rax,%r8
    xchgq %rax,%rax
    xchgq %r8,%r9
    xchgq %r10,%r11
    xchgb %al,16(%r12)
    xchgw 8(%r13),%r8w
    xchgq %r10,external(%rip)
    xchgq external(%rip),%r12
    xchg %r8,%r9

    popcntq external(%rip),%rax
    movl %r8d,external(%rip)
    ret
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/forms.s"
as --64 -o "$work/gnu.o" "$work/forms.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
if ! cmp "$work/mini.bin" "$work/gnu.bin"; then
    echo 'MiniAS x86 form bytes:' >&2
    objdump -dr "$work/mini.o" >&2
    echo 'GNU x86 form bytes:' >&2
    objdump -dr "$work/gnu.o" >&2
    exit 1
fi
python3 - "$work/gnu.o" "$work/mini.o" <<'PY'
import re
import subprocess
import sys

def relocs(path):
    values = []
    section = ''
    for line in subprocess.check_output(['readelf', '-Wr', path], text=True).splitlines():
        match = re.match(r"Relocation section '([^']+)'", line)
        if match:
            section = match.group(1)
            continue
        fields = line.split()
        if (section == '.rela.text' and len(fields) >= 7 and
                re.fullmatch('[0-9a-fA-F]{8,16}', fields[0]) and
                fields[2].startswith('R_X86_64_')):
            values.append((int(fields[0], 16), fields[2],
                           fields[4], tuple(fields[5:])))
    return sorted(values)

gnu, mini = (relocs(path) for path in sys.argv[1:])
if len(gnu) < 6:
    raise SystemExit(f'GNU form oracle unexpectedly sparse: {gnu}')
if gnu != mini:
    raise SystemExit(f'x86 typed form relocations differ\nGNU: {gnu}\nMiniAS: {mini}')
print(f'GNU typed-form relocations exact: {len(gnu)}')
PY

# Each negative is checked separately. Success means an invalid form was
# accidentally accepted; failure must not leave a partial output object.
invalid=(
    'btb %al,%bl'
    'xaddq %rax,%ebx'
    'cmpxchgb %ah,(%r8)'
    'xchgq (%rax),(%rbx)'
    'xchgw %ax,%eax'
    'xaddb %spl,%ah'
)
for i in "${!invalid[@]}"; do
    printf '.text\n%s\n' "${invalid[i]}" >"$work/bad.s"
    if as --64 -o "$work/bad-gnu.o" "$work/bad.s" >"$work/gnu.err" 2>&1; then
        echo "GNU unexpectedly accepted invalid form: ${invalid[i]}" >&2; exit 1
    fi
    if "$MINIAS" --target=x86_64 -o "$work/bad-mini.o" "$work/bad.s" >"$work/mini.err" 2>&1; then
        echo "MiniAS unexpectedly accepted invalid form: ${invalid[i]}" >&2; exit 1
    fi
    test ! -e "$work/bad-mini.o"
done
echo 'MINIAS_X86_MODRM_FORM=PASS GNU_bytes=exact GNU_relocs=exact forms=63 negatives=6'
