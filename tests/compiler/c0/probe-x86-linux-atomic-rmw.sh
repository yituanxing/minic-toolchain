#!/usr/bin/env bash
# GNU Linux 6.6.143 x86 atomic_{add,sub,and,or,xor} 32/64 bit, SMP/local.
# Differential executes real source GCC inline ASM and MiniC-produced GAS.
set -Eeuo pipefail
minic=${1:?MiniC compiler required}
w=$(mktemp -d); trap 'rm -rf "$w"' EXIT
python3 - "$w" <<'PY'
import pathlib,sys
w=pathlib.Path(sys.argv[1])
prototypes=[]; body=[]; checks=[]
for width,typ in [(32,"unsigned int"),(64,"unsigned long long")]:
    for mode in ("smp","local"):
        for op in ("add","sub","and","or","xor"):
            n=f"{op}_{width}_{mode}"
            mnemonic=op+("l" if width==32 else "q")
            prefix=('.pushsection .smp_locks,\\\"a\\\"\\n'
                    '.balign 4\\n.long 671f - .\\n'
                    '.popsection\\n671:\\n\\tlock; ') if mode=="smp" else ''
            for p in ("mini","gnu"):
                body.append(f"""
void {p}_{n}({typ} *dst,{typ} value) {{
    asm volatile("{prefix}{mnemonic} %1, %0"
                 : "+m"(*dst) : "ir"(value) : "memory");
}}""")
            prototypes.extend([f"extern void mini_{n}({typ}*,{typ});",
                               f"extern void gnu_{n}({typ}*,{typ});"])
            checks.append(f"""
for(int i=0;i<16;++i)for(int j=0;j<16;++j){{
  {typ} a=({typ})(i*8971-18123),b=a,v=({typ})(j*6613-42391);
  mini_{n}(&a,v);gnu_{n}(&b,v);
  if(a!=b){{printf("DIFF {n} %d %d\\n",i,j);return 1;}}
}}""")

# Original x86 atomic64_64.h: add/sub use '=m' output + 'er' value
# plus an explicit extra 'm' address dependency; inc/dec use '=m'+'m'.
# Original 32-bit atomic.h unary inc/dec are '+m', no separate input.
for width,typ in [(32,"int"),(64,"long long")]:
    for op in ("inc","dec"):
        n=f"unary_{op}_{width}"
        mnemonic=op+("l" if width==32 else "q")
        prefix=('.pushsection .smp_locks,\\\"a\\\"\\n'
                '.balign 4\\n.long 671f - .\\n'
                '.popsection\\n671:\\n\\tlock; ')
        for p in ("mini","gnu"):
            if width==64:
                body.append(f'''
void {p}_{n}({typ} *dst) {{
    asm volatile("{prefix}{mnemonic} %0"
                 : "=m"(*dst) : "m"(*dst) : "memory");
}}''')
            else:
                body.append(f'''
void {p}_{n}({typ} *dst) {{
    asm volatile("{prefix}{mnemonic} %0"
                 : "+m"(*dst) : : "memory");
}}''')
        prototypes.extend([f"extern void mini_{n}({typ}*);",
                           f"extern void gnu_{n}({typ}*);"])
        checks.append(f'''
for(int i=0;i<16;++i)for(int j=0;j<16;++j){{
    {typ} a=({typ})(i*8971-j*27),b=a;
    mini_{n}(&a);gnu_{n}(&b);
    if(a!=b){{printf("DIFF {n} %d %d\\n",i,j);return 1;}}
}}''')
for op in ("add","sub"):
    n=f"legacy_{op}_64"
    prefix=('.pushsection .smp_locks,\\\"a\\\"\\n'
            '.balign 4\\n.long 671f - .\\n'
            '.popsection\\n671:\\n\\tlock; ')
    for p in ("mini","gnu"):
        body.append(f'''
void {p}_{n}(long long *dst,long long value) {{
    asm volatile("{prefix}{op}q %1,%0"
                 : "=m"(*dst) : "er"(value), "m"(*dst) : "memory");
}}''')
    prototypes.extend([f"extern void mini_{n}(long long*,long long);",
                       f"extern void gnu_{n}(long long*,long long);"])
    checks.append(f'''
for(int i=0;i<16;++i)for(int j=0;j<16;++j){{
    long long a=(long long)(i*8971-j*27),b=a,v=(long long)(j*6613-42391);
    mini_{n}(&a,v);gnu_{n}(&b,v);
    if(a!=b){{printf("DIFF {n} %d %d\\n",i,j);return 1;}}
}}''')

src="\n".join(body)
(w/"both.c").write_text(src)
(w/"mini.i").write_text(src.replace("gnu_","hidden_gnu_"))
(w/"gnu.c").write_text(src.replace("mini_","hidden_mini_"))
(w/"driver.c").write_text("#include <stdio.h>\n"+"\n".join(prototypes)+
  "\nint main(void) {\n"+"\n".join(checks)+"\nreturn 42;\n}\n")
# One valid GNU Linux SMP atomic instruction may not hide arbitrary code.
(w/"bad-extra.i").write_text(src.replace("addl %1, %0",
       "addl %1, %0; cli",1).replace("gnu_","hidden_gnu_"))
# Wrong memory width relative to addl must not emit an invalid 32-bit
# write into a 64-bit C object.
(w/"bad-width.i").write_text((w/"mini.i").read_text().replace(
    "void mini_add_32_smp(unsigned int *dst,unsigned int value)",
    "void mini_add_32_smp(unsigned long long *dst,unsigned int value)",1))
PY
"$minic" --target=x86_64-linux -S "$w/mini.i" -o "$w/mini.s"
cc -O0 -no-pie "$w/driver.c" "$w/gnu.c" -x assembler "$w/mini.s" -o "$w/diff"
rc=0; "$w/diff" || rc=$?
test "$rc" -eq 42 || { echo "x86 RMW GNU differential failed $rc" >&2; exit 1; }
cc -c -x assembler "$w/mini.s" -o "$w/mini.o"
objdump -h "$w/mini.o" | grep -Fq '.smp_locks'
for op in addl subl andl orl xorl addq subq andq orq xorq;do
  grep -Fq "$op" "$w/mini.s"
done
for bad in bad-extra bad-width;do
  if "$minic" --target=x86_64-linux -S "$w/$bad.i" \
    -o "$w/$bad.s" >/dev/null 2>&1;then
    echo "x86 RMW accepted $bad" >&2; exit 1
  fi
  test ! -e "$w/$bad.s"
done
echo 'MINIC_X86_LINUX_ATOMIC_RMW=PASS alu=20 unary=4 legacy64=2 GNU_pairs=6656 negative=2'
