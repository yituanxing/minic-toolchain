#!/usr/bin/env bash
set -Eeuo pipefail
minic=$1; work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cat >"$work/bitops.i" <<'EOF'
typedef _Bool bool;
bool mini_test(unsigned long *p,long n) {
 bool old; asm volatile(" btq %2,%1\n\t/* output condition code c */\n"
   : "=@ccc"(old): "m"(*p),"Ir"(n):"memory");return old;
}
bool mini_test_set(unsigned long *p,long n) {
 bool old; asm(" btsq %2,%1\n\t/* output condition code c */\n"
   : "=@ccc"(old): "m"(*p),"Ir"(n):"memory");return old;
}
bool mini_test_clear(unsigned long *p,long n) {
 bool old; asm volatile(" btrq %2,%1\n\t/* output condition code c */\n"
   : "=@ccc"(old): "m"(*p),"Ir"(n):"memory");return old;
}
bool mini_test_change(unsigned long *p,long n) {
 bool old; asm volatile(" btcq %2,%1\n\t/* output condition code c */\n"
   : "=@ccc"(old): "m"(*p),"Ir"(n):"memory");return old;
}
void mini_set(unsigned long *p,long n) {
 asm volatile(" btsq %1,%0"::"m"(*p),"Ir"(n):"memory");
}
void mini_clear(unsigned long *p,long n) {
 asm volatile(" btrq %1,%0"::"m"(*p),"Ir"(n):"memory");
}
void mini_lock_set(unsigned long *p,long n) {
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "btsq"
  " %1,%0"::"m"(*p),"Ir"(n):"memory");
}
void mini_lock_clear(unsigned long *p,long n) {
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "btrq"
  " %1,%0"::"m"(*p),"Ir"(n):"memory");
}
bool mini_lock_test_set(unsigned long *p,long n) {
 bool old; asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "btsq"
  " %[val], %[var]\n\t/* output condition code c */\n"
  :[var] "+m"(*p),"=@ccc"(old):[val] "Ir"(n):"memory");return old;
}
bool mini_lock_test_clear(unsigned long *p,long n) {
 bool old; asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "btrq"
  " %[val], %[var]\n\t/* output condition code c */\n"
  :[var] "+m"(*p),"=@ccc"(old):[val] "Ir"(n):"memory");return old;
}
void mini_lock_or_byte(volatile char *p,int mask) {
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; "
  "orb %b1,%0" : "+m"(*p) : "iq"(mask) : "memory");
}
void mini_lock_and_byte(volatile char *p,int mask) {
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; "
  "andb %b1,%0" : "+m"(*p) : "iq"(mask));
}
EOF
sed 's/mini_/gnu_/g' "$work/bitops.i" >"$work/gnu.c"
cat >"$work/main.c" <<'EOF'
#include <string.h>
typedef _Bool bool;
#define DECL(prefix) \
extern bool prefix##_test(unsigned long *,long),prefix##_test_set(unsigned long *,long),\
 prefix##_test_clear(unsigned long *,long),prefix##_test_change(unsigned long *,long),\
 prefix##_lock_test_set(unsigned long *,long),prefix##_lock_test_clear(unsigned long *,long);\
extern void prefix##_set(unsigned long *,long),prefix##_clear(unsigned long *,long),\
 prefix##_lock_set(unsigned long *,long),prefix##_lock_clear(unsigned long *,long)
DECL(mini);DECL(gnu);
extern void mini_lock_or_byte(volatile char*,int),gnu_lock_or_byte(volatile char*,int);
extern void mini_lock_and_byte(volatile char*,int),gnu_lock_and_byte(volatile char*,int);
#define CHECK_BOOL(name) \
 do { bool x=mini_##name(a,n), y=gnu_##name(b,n); \
 if(x!=y||memcmp(a,b,sizeof(a))!=0)return 1; }while(0)
#define CHECK_VOID(name) \
 do { mini_##name(a,n);gnu_##name(b,n);\
 if(memcmp(a,b,sizeof(a))!=0)return 2; }while(0)
int main(void) {
 long n; for(n=0;n<128;n+=7){
  unsigned long a[2]={0x101UL,0xf000000000000000UL};
  unsigned long b[2]={a[0],a[1]};
  CHECK_BOOL(test);CHECK_VOID(set);CHECK_BOOL(test_set);
  CHECK_VOID(clear);CHECK_BOOL(test_clear);CHECK_BOOL(test_change);
  CHECK_VOID(lock_set);CHECK_BOOL(lock_test_set);
  CHECK_VOID(lock_clear);CHECK_BOOL(lock_test_clear);
  mini_lock_or_byte((char *)a,1<<(n&7));
  gnu_lock_or_byte((char *)b,1<<(n&7));
  if(memcmp(a,b,sizeof(a))!=0)return 3;
  mini_lock_and_byte((char *)a,~(1<<(n&7)));
  gnu_lock_and_byte((char *)b,~(1<<(n&7)));
  if(memcmp(a,b,sizeof(a))!=0)return 4;
 } return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$work/bitops.i" -o "$work/bitops.s"
for op in 'lock; btsq' 'lock; btrq' 'lock; orb %r10b' 'lock; andb %r10b' \
          'btq %r10' 'btsq %r10' 'btrq %r10' 'btcq %r10' 'setc %al'; do
 grep -Fq "$op" "$work/bitops.s"
done
cc -O0 -no-pie "$work/main.c" "$work/gnu.c" -x assembler "$work/bitops.s" -o "$work/oracle"
set +e; "$work/oracle"; rc=$?; set -e
test "$rc" -eq 42 || { echo "FAIL x86 Linux BT/LOCK GNU native diff rc=$rc"; exit 1; }
cc -O0 -c -x assembler "$work/bitops.s" -o "$work/bitops.o"
objdump -h "$work/bitops.o" | grep -Fq '.smp_locks'
cat >"$work/bad.i" <<'EOF'
typedef _Bool bool;
bool bad(unsigned long *p,long n) {
 bool old; asm volatile("btq %2,%1; cli" :
 "=@ccc"(old):"m"(*p),"Ir"(n):"memory");return old;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/bad.i" -o "$work/bad.s" \
   >/dev/null 2>&1; then echo 'FAIL injected x86 bitops admitted';exit 1;fi
echo 'MINIC_X86_LINUX_BITOPS_GNU_NATIVE=PASS variants=12 n_cases=19 hardware_cf=1 smp_locks=1 injected_rejected=1'
