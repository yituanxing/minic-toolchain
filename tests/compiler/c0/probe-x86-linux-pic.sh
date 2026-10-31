#!/usr/bin/env bash
# Linux 6.6.143 original x86_64 Kbuild has 11 -fPIC/-fPIE owners;
# x86 PIC uses GOTPCREL for preemptible globals and function addresses and PLT
# for symbol CALL; non-PIC uses direct PC-relative address materialization.
# Prove static and dynamic GNU AS/LD/runtime semantics against independent GCC.
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
# Linux kernel owners use -fno-PIE: even default-visible undefined externals
# may use direct RIP-relative LEA, whereas a shared DSO must retain GOT access
# for preemptible references. Exercise both variable and function addresses.
cat > "$d/address-policy.i" <<'SRC'
extern int public_value;
extern int public_plus(int);
extern __attribute__((visibility("hidden"))) int hidden_value;
extern __attribute__((visibility("hidden"))) int hidden_plus(int);
int *mini_public_ptr(void){return &public_value;}
int (*mini_public_fn(void))(int){return &public_plus;}
int *mini_hidden_ptr(void){return &hidden_value;}
int (*mini_hidden_fn(void))(int){return &hidden_plus;}
SRC
sed 's/mini_/gnu_/g' "$d/address-policy.i" > "$d/address-gnu.c"
cat > "$d/address-provider.c" <<'SRC'
int public_value=17;
int public_plus(int x){return x*3+1;}
__attribute__((visibility("hidden"))) int hidden_value=23;
__attribute__((visibility("hidden"))) int hidden_plus(int x){return x-7;}
SRC
"$mini" --target=x86_64-linux -fno-PIE -S "$d/address-policy.i" -o "$d/address-nopic.s"
gcc -c -x assembler "$d/address-nopic.s" -o "$d/address-nopic.o"
readelf -rW "$d/address-nopic.o" > "$d/address-nopic.reloc"
for symbol in public_value public_plus hidden_value hidden_plus;do
 grep -E "R_X86_64_PC32.*[[:space:]]$symbol([[:space:]]|$)" "$d/address-nopic.reloc" >/dev/null
done
if grep -E 'GOTPCREL|_GLOBAL_OFFSET_TABLE_' "$d/address-nopic.reloc";then
 echo 'non-PIC x86 emitted a GOT relocation' >&2;exit 1
fi
gcc -O0 -fno-pic -fno-pie -c "$d/address-gnu.c" -o "$d/address-gnu.o"
cat > "$d/address-host.c" <<'SRC'
#include <stdio.h>
extern int public_value,public_plus(int),hidden_value,hidden_plus(int);
extern int *mini_public_ptr(void),*gnu_public_ptr(void);
extern int *mini_hidden_ptr(void),*gnu_hidden_ptr(void);
extern int (*mini_public_fn(void))(int),(*gnu_public_fn(void))(int);
extern int (*mini_hidden_fn(void))(int),(*gnu_hidden_fn(void))(int);
int main(void){
 if(mini_public_ptr()!=&public_value||gnu_public_ptr()!=&public_value)return 1;
 if(mini_hidden_ptr()!=&hidden_value||gnu_hidden_ptr()!=&hidden_value)return 2;
 if(mini_public_fn()!=public_plus||gnu_public_fn()!=public_plus)return 3;
 if(mini_hidden_fn()!=hidden_plus||gnu_hidden_fn()!=hidden_plus)return 4;
 if(*mini_public_ptr()!=17||*mini_hidden_ptr()!=23)return 5;
 if(mini_public_fn()(11)!=gnu_public_fn()(11)||mini_public_fn()(11)!=34)return 6;
 if(mini_hidden_fn()(11)!=gnu_hidden_fn()(11)||mini_hidden_fn()(11)!=4)return 7;
 return 0;
}
SRC
gcc -no-pie "$d/address-nopic.o" "$d/address-gnu.o" \
 "$d/address-provider.c" "$d/address-host.c" -o "$d/address-nopic-host"
"$d/address-nopic-host"
"$mini" --target=x86_64-linux -fPIC -S "$d/address-policy.i" -o "$d/address-pic.s"
gcc -fPIC -c -x assembler "$d/address-pic.s" -o "$d/address-pic.o"
readelf -rW "$d/address-pic.o" > "$d/address-pic.reloc"
for symbol in public_value public_plus;do
 grep -E "R_X86_64_(REX_)?GOTPCREL(X)?.*[[:space:]]$symbol([[:space:]]|$)" "$d/address-pic.reloc" >/dev/null
done
if grep -E 'GOTPCREL.*[[:space:]]hidden_(value|plus)([[:space:]]|$)' "$d/address-pic.reloc";then
 echo 'hidden PIC address unexpectedly uses preemptible GOT' >&2;exit 1
fi
gcc -fPIC -c "$d/address-provider.c" -o "$d/address-provider-pic.o"
gcc -shared -fPIC "$d/address-pic.o" "$d/address-provider-pic.o" -o "$d/libaddress-pic.so"
readelf -rW "$d/libaddress-pic.so" > "$d/address-shared.reloc"
for symbol in public_value public_plus;do
 grep -E "R_X86_64_GLOB_DAT.*[[:space:]]$symbol([[:space:]]|$)" "$d/address-shared.reloc" >/dev/null
done
# RV32 now has its own scoped RISC-V GOT PIC lowering, independently covered
# by the target-language ABI options probe; all five targets accept PIC.
cat > "$d/invalid.i" <<'SRC'
int value(void){return 42;}
SRC
"$mini" --target=riscv32-linux -fPIC -S "$d/invalid.i" -o "$d/rv32-pic.s"
test -s "$d/rv32-pic.s"
echo "MINIC_X86_PIC=PASS GOTPCREL=1 PLT=1 dynamic_DSO_GNU_diff=1 PIE_ASLR=1 nonPIC_PC32_GNU_native=1 PIC_public_GOT_DSO=1 hidden_direct=1 rv32_pic=qualified"
