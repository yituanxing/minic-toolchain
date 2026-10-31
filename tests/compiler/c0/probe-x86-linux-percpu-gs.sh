#!/usr/bin/env bash
# Original Linux percpu.h 36 whole GS MOV/INC/DEC/ALU hardware operations.
set -Eeuo pipefail
cc=$1;d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
python3 - "$d" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
ops=["read","write","inc","dec","add","sub","and","or","xor"]
types=[("b","unsigned char",1),("w","unsigned short",2),
       ("l","unsigned int",4),("q","unsigned long long",8)]
def definitions(prefix):
 out=[]
 for suf,ty,sz in types:
  for op in ops:
   name=f"{prefix}_{op}_{suf}"
   if op=="read":
    src=f'''{ty} {name}(volatile {ty} *mem){{
      {ty} val=0;
      asm("mov{suf} %%gs:%1, %0" : "={'q' if sz==1 else 'r'}"(val) : "m"(*mem));
      return val;
    }}'''
   elif op in ("inc","dec"):
    src=f'''void {name}(volatile {ty} *mem){{
      asm("{op}{suf} %%gs:%0" : "+m"(*mem));
    }}'''
   else:
    ins="mov" if op=="write" else op
    c="qi" if sz==1 else "re" if sz==8 else "ri"
    src=f'''void {name}(volatile {ty} *mem,{ty} val){{
      asm("{ins}{suf} %1, %%gs:%0" : "+m"(*mem) : "{c}"(val));
    }}'''
   out.append(src)
 return "\n".join(out)+"\n"
(p/"mini.i").write_text(definitions("mini"))
(p/"gnu.c").write_text(definitions("gnu"))
(p/"bad-extra.i").write_text(definitions("mini").replace(
  '"movl %%gs:%1, %0"','"movl %%gs:%1, %0; cli"'))
(p/"bad-width.i").write_text(definitions("mini").replace(
  '"movl %%gs:%1, %0"','"movq %%gs:%1, %0"'))
(p/"bad-constraint.i").write_text(definitions("mini").replace(
  '"m"(*mem)','"r"(*mem)'))
# Deterministic real GS-base comparisons with two independent original
# GNU/MiniC functions applied to a real memory region.
driver=["#include <sys/syscall.h>","#include <unistd.h>",
 "#include <stdint.h>","#include <stdio.h>",
 "#define ARCH_SET_GS 0x1001",""]
for suf,ty,sz in types:
 for op in ops:
  name=f"{op}_{suf}"
  args=f"volatile {ty} *" if op in ("read","inc","dec") else f"volatile {ty} *,{ty}"
  ret=ty if op=="read" else "void"
  driver += [f"extern {ret} mini_{name}({args});",f"extern {ret} gnu_{name}({args});"]
driver += ["int main(void){","  union { unsigned long long u64; unsigned char bytes[8]; } memory;",
           "  if(syscall(SYS_arch_prctl,ARCH_SET_GS,(unsigned long)&memory)!=0)return 1;"]
for suf,ty,sz in types:
 for op in ops:
  name=f"{op}_{suf}";value=("'"+"42"+"'" if False else "0x35")
  driver += ["  /* CASE_START */", "  memory.u64=0x123456789abcdef0ULL;"]
  ptr=f"(volatile {ty} *)(uintptr_t)0"
  if op=="read":
   driver += [f"  {ty} mini_read={''}mini_{name}({ptr});",
              f"  {ty} gnu_read=gnu_{name}({ptr});",
              "  if(mini_read!=gnu_read)return 2;"]
  else:
   arg="" if op in ("inc","dec") else f",({ty}){value}"
   driver += [f"  mini_{name}({ptr}{arg});",
              "  unsigned long long expected=memory.u64;",
              "  memory.u64=0x123456789abcdef0ULL;",
              f"  gnu_{name}({ptr}{arg});",
              "  if(memory.u64!=expected)return 3;"]
driver += ['  puts("MINIC_X86_GS_PERCPU_GNU_NATIVE=PASS forms=36");',
           "  return 42;","}"]
# Distinct local result variable names are in separate blocks.
# Each generated operation is wrapped in one C scope.
lines=driver; hdr=lines[:lines.index("int main(void){")+1]
scoped=[]
i=len(hdr)
while i<len(lines) and lines[i].startswith("  union"):scoped.append(lines[i]);i+=1
# main already declared, collect prelude
while i<len(lines) and ("syscall" in lines[i]):scoped.append(lines[i]);i+=1
body=lines[i:-3];cases=[];cur=[]
for line in body:
 if line=="  /* CASE_START */":
  if cur:cases.append(cur)
  cur=[]
 else:cur.append(line)
if cur:cases.append(cur)
driver=hdr+scoped+["\n".join(["  {"]+case+["  }"]) for case in cases]+[
 '  puts("MINIC_X86_GS_PERCPU_GNU_NATIVE=PASS forms=36");',
 "  return 42;","}"]
(p/"driver.c").write_text("\n".join(driver)+"\n")
PY
"$cc" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
grep -Fq 'movl %gs:(%r10), %eax' "$d/mini.s"
grep -Fq 'addl %eax, %gs:(%r10)' "$d/mini.s"
gcc -O0 -no-pie "$d/driver.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
rc=0;"$d/run" || rc=$?
test "$rc" -eq 42
for bad in bad-extra bad-width bad-constraint;do
 if "$cc" --target=x86_64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
   echo "FAIL unsafe percpu GS $bad accepted" >&2;exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo "MINIC_X86_PERCPU_GS=PASS forms=36 native=1 negative=3"
