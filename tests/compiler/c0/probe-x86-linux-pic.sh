#!/usr/bin/env bash
# Linux 6.6.143 original x86_64 Kbuild has 11 -fPIC/-fPIE owners;
# x86 machine already uses GOTPCREL for globals & function addresses and PLT
# for symbol CALL. Prove a genuine DSO and PIE with independent GNU GCC and
# the ELF dynamic loader before admitting the CLI feature for this target.
set -Eeuo pipefail
mini="$1";test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/minic.i" <<'SRC'
int exported_value=11;
static int static_value=13;
int *exported_pointer=&exported_value;
extern int imported_value;
extern int imported_add(int);
int mini_entry(int x) {
 return imported_add(x)+imported_value+exported_value+
        static_value+*exported_pointer;
}
int *mini_return_global(void) { return &exported_value; }
SRC
"$mini" --target=x86_64-linux -fPIC -S "$d/minic.i" -o "$d/minic.s"
grep -Fq '@GOTPCREL(%rip)' "$d/minic.s"
grep -Fq '@PLT' "$d/minic.s"
gcc -fPIC -shared -o "$d/libminic.so" -x assembler "$d/minic.s"
sed 's/mini_/gnu_/g' "$d/minic.i" > "$d/gnu.c"
gcc -fPIC -shared -O0 -o "$d/libgnu.so" "$d/gnu.c"
cat > "$d/host.c" <<'SRC'
#include <dlfcn.h>
#include <stdio.h>
int imported_value=17;
int imported_add(int x){return 3*x+19;}
typedef int (*entry_fn)(int);
typedef int *(*get_ptr_fn)(void);
static int test(const char *file,const char *prefix){
 void *handle=dlopen(file,RTLD_NOW|RTLD_LOCAL);
 if(!handle){fprintf(stderr,"dlopen: %s\n",dlerror());return -1;}
 entry_fn entry=(entry_fn)dlsym(handle,prefix[0]=='m'?"mini_entry":"gnu_entry");
 get_ptr_fn ptr=(get_ptr_fn)dlsym(handle,prefix[0]=='m'?"mini_return_global":"gnu_return_global");
 if(!entry||!ptr||*ptr()!=11)return -2;
 int result=entry(17);
 dlclose(handle);
 return result;
}
int main(int argc,char**argv){
 if(argc!=3)return 9;
 int m=test(argv[1],"mini"),g=test(argv[2],"gnu");
 if(m!=g||m!=3*17+19+17+11+13+11){fprintf(stderr,"%d/%d\n",m,g);return 3;}
 return 0;
}
SRC
gcc -O0 -rdynamic "$d/host.c" -ldl -o "$d/host"
"$d/host" "$d/libminic.so" "$d/libgnu.so"
# PIE is separate from shared DSO: the relocations need to resolve under
# address space randomization, not just a relocatable ELF .o.
"$mini" --target=x86_64-linux -fPIE -S "$d/minic.i" -o "$d/pie.s"
cat > "$d/piehost.c" <<'SRC'
extern int mini_entry(int),*mini_return_global(void);
int imported_value=17;
int imported_add(int x){return 3*x+19;}
int main(void){
 return mini_entry(17)==3*17+19+17+11+13+11
       && *mini_return_global()==11 ? 0:1;
}
SRC
gcc -fPIE -pie "$d/piehost.c" -x assembler "$d/pie.s" -o "$d/host-pie"
"$d/host-pie"
# The only still-unqualified target is RV32; AArch64 and LoongArch64
# now have their own PC-relative local/GOT external address lowering.
cat > "$d/invalid.i" <<'SRC'
int value(void){return 42;}
SRC
for target in riscv32;do
 if "$mini" --target="$target-linux" -fPIC -S "$d/invalid.i" -o "$d/bad.s" >/dev/null 2>&1;then
  echo "UNQUALIFIED_PIC_ACCEPTED=$target" >&2;exit 1
 fi
done
echo "MINIC_X86_PIC=PASS GOTPCREL=1 PLT=1 dynamic_DSO_GNU_diff=1 PIE_ASLR=1 rv32_unqualified=rejected"
