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
SRC
"$cc" --target=x86_64-linux -S "$d/xadd.i" -o "$d/xadd.s"
grep -Fq '  xaddq %r10, (%r11)' "$d/xadd.s"
grep -Fq '  xaddl %r10d, (%r11)' "$d/xadd.s"
sed 's/mini_/gnu_/g' "$d/xadd.i" > "$d/gnu.c"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
gcc -c -x assembler "$d/xadd.s" -o "$d/mini.o"
cat > "$d/main.c" <<'SRC'
extern long mini_xadd64(long,long*),gnu_xadd64(long,long*);
extern int mini_xadd32(int,int*),gnu_xadd32(int,int*);
int main(void) {
 for(int i=-42;i<=42;++i) for(int d=-7;d<=7;++d) {
  long a=i,b=i;
  long olda=mini_xadd64(d,&a),oldb=gnu_xadd64(d,&b);
  if(olda!=oldb||a!=b)return 1;
  int c=i,e=i;
  int oldc=mini_xadd32(d,&c),olde=gnu_xadd32(d,&e);
  if(oldc!=olde||c!=e)return 2;
 }
 return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
for bad in extra wrong-constraint wrong-width;do
 case "$bad" in
  extra) sed 's/xaddq %0, %1;/xaddq %0, %1; nop/' "$d/xadd.i" >"$d/bad.i";;
  wrong-constraint) sed 's/"+r"(delta)/"=r"(delta)/' "$d/xadd.i" >"$d/bad.i";;
  wrong-width) sed 's/xaddq %0, %1;/xaddl %0, %1;/' "$d/xadd.i" >"$d/bad.i";;
 esac
 if "$cc" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1;then
   echo "FAIL unsafe xadd $bad accepted" >&2;exit 1
 fi
 test ! -e "$d/bad.s"
done
echo 'MINIC_X86_LOCAL_XADD=PASS original_linux=1 two_native_widths=1 GNU_differential_pairs=2550 negatives=3'
