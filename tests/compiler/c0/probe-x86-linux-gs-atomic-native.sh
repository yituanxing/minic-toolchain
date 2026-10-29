#!/usr/bin/env bash
# Original Linux 6.6.143 __this_cpu_{add_return,cmpxchg,xchg} carrier patterns.
# The GNU oracle and MiniC both access real %gs-based memory; compare values,
# previous-value results and failed/successful compare-and-exchange effects.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
python3 - "$d" <<'PY'
from pathlib import Path
import sys
d=Path(sys.argv[1])
widths=[('b','unsigned char','q'),('w','unsigned short','r'),
        ('l','unsigned int','r'),('q','unsigned long','r')]
defs=[]
driver=['#include <sys/syscall.h>','#include <unistd.h>',
        '#include <stdint.h>','#define ARCH_SET_GS 0x1001']
checks=[]
for suf,ty,reg in widths:
    defs += [f'''{ty} mini_xadd_{suf}(volatile {ty} *p, {ty} value) {{
      asm("xadd{suf} %[tmp], %%gs:%[var]"
          : [tmp] "+{reg}"(value), [var] "+m"(*p) :: "memory");
      return value;
    }}''',f'''{ty} mini_cas_{suf}(volatile {ty} *p, {ty} old, {ty} value) {{
      asm("cmpxchg{suf} %[nval], %%gs:%[var]"
          : [oval] "+a"(old), [var] "+m"(*p)
          : [nval] "{reg}"(value) : "memory");
      return old;
    }}''',f'''{ty} mini_loop_{suf}(volatile {ty} *p, {ty} value) {{
      {ty} old;
      asm volatile("mov{suf} %%gs:%[var], %[oval]\\n"
          "1:\\tcmpxchg{suf} %[nval], %%gs:%[var]\\n\\tjnz 1b"
          : [oval] "=&a"(old), [var] "+m"(*p)
          : [nval] "{reg}"(value) : "memory");
      return old;
    }}''']
    for prefix in ('mini','gnu'):
        for kind,args in [('xadd',f'volatile {ty} *, {ty}'),
                          ('cas',f'volatile {ty} *, {ty}, {ty}'),
                          ('loop',f'volatile {ty} *, {ty}')]:
            driver.append(f'extern {ty} {prefix}_{kind}_{suf}({args});')
    cast=f'(volatile {ty} *)(uintptr_t)0'
    checks.append(f'''{{
       volatile {ty} *p={cast};
       storage=37;
       {ty} m=mini_xadd_{suf}(p, 11);
       unsigned long long changed=storage;
       storage=37;
       {ty} g=gnu_xadd_{suf}(p, 11);
       if (m!=g || changed!=storage || storage!=48) return 11;
       storage=37;
       m=mini_cas_{suf}(p, 37, 22);
       changed=storage;
       storage=37;
       g=gnu_cas_{suf}(p, 37, 22);
       if (m!=g || changed!=storage || storage!=22) return 12;
       storage=37;
       m=mini_cas_{suf}(p, 19, 22);
       changed=storage;
       storage=37;
       g=gnu_cas_{suf}(p, 19, 22);
       if (m!=g || changed!=storage || storage!=37) return 13;
       storage=37;
       m=mini_loop_{suf}(p, 16);
       changed=storage;
       storage=37;
       g=gnu_loop_{suf}(p, 16);
       if (m!=g || changed!=storage || storage!=16) return 14;
    }}''')
source='\n'.join(defs)+'\n'
(d/'mini.i').write_text(source)
(d/'gnu.c').write_text(source.replace('mini_', 'gnu_'))
(d/'bad.i').write_text(source.replace('cmpxchgl %[nval], %%gs:%[var]',
                                      'cmpxchgl %[nval], %%gs:%[var]; cli'))
(d/'bad-loop.i').write_text(source.replace('jnz 1b', 'jz 1b'))
(d/'bad-segment.i').write_text(source.replace(
    'xaddq %[tmp], %%gs:%[var]', 'xaddq %[tmp], %%fs:%[var]'))
(d/'bad-carrier.i').write_text(source.replace(
    '[oval] "+a"(old)', '[oval] "=a"(old)'))
driver += ['int main(void) {','  volatile unsigned long long storage=0;',
           '  if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)&storage)) return 2;',
           *checks,'  return 0;','}']
(d/'driver.c').write_text('\n'.join(driver)+'\n')
PY
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
grep -Fq 'xaddb ' "$d/mini.s"
grep -Fq 'cmpxchgq ' "$d/mini.s"
grep -Fq '%gs:' "$d/mini.s"
gcc -O0 -no-pie "$d/driver.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
for negative in bad bad-loop bad-segment bad-carrier; do
    if "$mini" --target=x86_64-linux -S "$d/$negative.i" \
        -o "$d/$negative.s" >/dev/null 2>&1; then
        echo "X86_GS_ATOMIC_FAIL=unsafe $negative variant accepted" >&2
        exit 1
    fi
    test ! -e "$d/$negative.s"
done
echo 'MINIC_X86_GS_ATOMIC=PASS forms=12 GNU_native=12 negative=4'
