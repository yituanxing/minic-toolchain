#!/usr/bin/env bash
# Genuine Linux 6.6.143 local_add_return uses xaddq +r/+m, memory clobber.
# Verify register old-value and memory updated sum against independent GCC.
set -Eeuo pipefail
cc=${1:?MiniC compiler}
test -x "$cc"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/xadd.i" <<'SRC'
long mini_xadd64(long delta, long *p) {
 asm volatile(" xaddq %0, %1;" : "+r"(delta), "+m"(*p) : : "memory");
 return delta;
}
int mini_xadd32(int delta, int *p) {
 asm volatile(" xaddl %0, %1;" : "+r"(delta), "+m"(*p) : : "memory");
 return delta;
}
long mini_xadd64_long_ll(long delta, long long *p) {
 asm volatile(" xaddq %0, %1;" : "+r"(delta), "+m"(*p) : : "memory");
 return delta;
}
long long mini_xadd64_ll_long(long long delta, long *p) {
 asm volatile(" xaddq %0, %1;" : "+r"(delta), "+m"(*p) : : "memory");
 return delta;
}
SRC
"$cc" --target=x86_64-linux -S "$d/xadd.i" -o "$d/xadd.s"
for form in '  xaddq %rax, (%r10)' '  xaddl %eax, (%r10)'; do
  if ! grep -Fq "$form" "$d/xadd.s"; then
    echo "FAIL: original Linux local XADD MachineIR physical form missing: $form" >&2
    sed -n '1,160p' "$d/xadd.s" >&2
    exit 1
  fi
done
sed 's/mini_/gnu_/g' "$d/xadd.i" > "$d/gnu.c"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
gcc -c -x assembler "$d/xadd.s" -o "$d/mini.o"
cat > "$d/main.c" <<'SRC'
extern long mini_xadd64(long,long*),gnu_xadd64(long,long*);
extern int mini_xadd32(int,int*),gnu_xadd32(int,int*);
extern long mini_xadd64_long_ll(long,long long*),gnu_xadd64_long_ll(long,long long*);
extern long long mini_xadd64_ll_long(long long,long*),gnu_xadd64_ll_long(long long,long*);
int main(void) {
 for(int i=-42;i<=42;++i) for(int d=-7;d<=7;++d) {
  long a=i,b=i;
  long olda=mini_xadd64(d,&a),oldb=gnu_xadd64(d,&b);
  if(olda!=oldb||a!=b)return 1;
  int c=i,e=i;
  int oldc=mini_xadd32(d,&c),olde=gnu_xadd32(d,&e);
  if(oldc!=olde||c!=e)return 2;
  long long f=i,g=i;
  long oldf=mini_xadd64_long_ll(d,&f),oldg=gnu_xadd64_long_ll(d,&g);
  if(oldf!=oldg||f!=g)return 3;
  long h=i,j=i;
  long long oldh=mini_xadd64_ll_long(d,&h),oldj=gnu_xadd64_ll_long(d,&j);
  if(oldh!=oldj||h!=j)return 4;
 }
 return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
for bad in extra wrong-constraint wrong-width wrong-memory-width;do
 case "$bad" in
  extra) sed 's/xaddq %0, %1;/xaddq %0, %1; nop/' "$d/xadd.i" >"$d/bad.i";;
  wrong-constraint) sed 's/"+r"(delta)/"=r"(delta)/' "$d/xadd.i" >"$d/bad.i";;
  wrong-width) sed 's/xaddq %0, %1;/xaddl %0, %1;/' "$d/xadd.i" >"$d/bad.i";;
  wrong-memory-width) sed 's/long long \*p)/int *p)/' "$d/xadd.i" >"$d/bad.i";;
 esac
 if MINIC_ASM_FRONTIER_TRACE=1 "$cc" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >"$d/bad.stdout" 2>"$d/bad.stderr";then
   echo "FAIL unsafe xadd $bad accepted" >&2;exit 1
 fi
 # C operand size belongs to source Target admission, not Core IR. Verify
 # this wrong-width GNU ISA form is rejected at the qualified target gate
 # instead of mistaking an unrelated parser or missing-tool failure for PASS.
 if [ "$bad" = wrong-memory-width ] &&
    { ! grep -Fq 'CORE_ASM_FRONTIER function=mini_xadd64_long_ll reason=target_required' "$d/bad.stderr" ||
      ! grep -Fq "Core IR does not yet support function 'mini_xadd64_long_ll'" "$d/bad.stderr"; };then
    echo 'FAIL wrong-width local XADD did not fail its source Target qualification gate' >&2
    sed -n '1,80p' "$d/bad.stderr" >&2
    exit 1
 fi
 test ! -e "$d/bad.s"
done
echo 'MINIC_X86_LOCAL_XADD=PASS original_linux=1 two_native_widths=1 mixed_64bit_ranks=2 GNU_differential_pairs=5100 negatives=4'
