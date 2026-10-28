#!/usr/bin/env bash
# Real Linux 6.6.143 native_cpuid: two tied EAX/ECX inputs; four outputs.
# The existing eight-operand model remains the same target ISA owner.
set -Eeuo pipefail
minic=$1; d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat >"$d/cpuid.i" <<'EOF'
void mini_cpuid(unsigned int *eax,unsigned int *ebx,
                unsigned int *ecx,unsigned int *edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "0"(*eax), "2"(*ecx) : "memory");
}
EOF
sed 's/mini_cpuid/gnu_cpuid/g' "$d/cpuid.i" > "$d/gnu.c"
cat >"$d/main.c" <<'EOF'
extern void mini_cpuid(unsigned int*,unsigned int*,unsigned int*,unsigned int*);
extern void gnu_cpuid(unsigned int*,unsigned int*,unsigned int*,unsigned int*);
int main(void){
    static const unsigned int leaves[]={0,1,2,7,0x80000000U};
    unsigned int i,sub;
    for(i=0;i<5;i++)for(sub=0;sub<4;sub++){
        unsigned int a=leaves[i],b=0,c=sub,d=0;
        unsigned int w=a,x=b,y=c,z=d;
        mini_cpuid(&a,&b,&c,&d);gnu_cpuid(&w,&x,&y,&z);
        if(a!=w||b!=x||c!=y||d!=z)return 1;
    }
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$d/cpuid.i" -o "$d/cpuid.s"
grep -Fq 'pushq %rbx' "$d/cpuid.s"
grep -Fq '  cpuid' "$d/cpuid.s"
grep -Fq 'popq %rbx' "$d/cpuid.s"
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/cpuid.s" -o "$d/diff"
set +e
"$d/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL original Linux cpuid GNU native rc=$rc"; exit 1; }
cat > "$d/unsafe.i" <<'EOF'
void bad(unsigned int *eax,unsigned int *ebx,
         unsigned int *ecx,unsigned int *edx) {
    asm volatile("cpuid; cli"
        : "=a"(*eax),"=b"(*ebx),"=c"(*ecx),"=d"(*edx)
        : "0"(*eax),"2"(*ecx) : "memory");
}
EOF
if "$minic" --target=x86_64-linux -S "$d/unsafe.i" \
    -o "$d/unsafe.s" >"$d/unsafe.log" 2>&1; then
    echo 'FAIL x86 unsafe CPUID instruction suffix admitted'; exit 1;
fi
echo 'MINIC_X86_LINUX_NATIVE_CPUID=PASS six_operands=1 native_pairs=20 preserves_rbx=1 negative=1'
