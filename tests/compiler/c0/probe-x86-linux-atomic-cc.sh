#!/usr/bin/env bash
set -Eeuo pipefail
minic=$1; work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/atomic.i" <<'EOF'
typedef _Bool bool;
typedef struct {int counter;} atomic_t;
typedef struct {long long counter;} atomic64_t;
bool mini_dec32(atomic_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "decl"
  " " "%[var]" "\n\t/* output condition code " "e" "*/\n"
  : [var] "+m"(v->counter), "=@cc" "e"(c) :: "memory");
 return c;
}
bool mini_inc32(atomic_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "incl"
  " " "%[var]" "\n\t/* output condition code " "e" "*/\n"
  : [var] "+m"(v->counter), "=@cc" "e"(c) :: "memory");
 return c;
}
bool mini_dec64(atomic64_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "decq"
  " " "%[var]" "\n\t/* output condition code " "e" "*/\n"
  : [var] "+m"(v->counter), "=@cc" "e"(c) :: "memory");
 return c;
}
bool mini_inc64(atomic64_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
  ".long 671f - .\n" ".popsection\n" "671:" "\n\tlock; " "incq"
  " " "%[var]" "\n\t/* output condition code " "e" "*/\n"
  : [var] "+m"(v->counter), "=@cc" "e"(c) :: "memory");
 return c;
}
EOF
sed 's/mini_/gnu_/g' "$work/atomic.i" > "$work/gnu.c"
cat >"$work/main.c" <<'EOF'
typedef struct {int counter;} atomic_t;
typedef struct {long long counter;} atomic64_t;
extern _Bool mini_dec32(atomic_t*),mini_inc32(atomic_t*);
extern _Bool mini_dec64(atomic64_t*),mini_inc64(atomic64_t*);
extern _Bool gnu_dec32(atomic_t*),gnu_inc32(atomic_t*);
extern _Bool gnu_dec64(atomic64_t*),gnu_inc64(atomic64_t*);
int main(void) {
 int v;
 for(v=-3;v<=3;v++){
  atomic_t a={v},b={v};
  if(mini_dec32(&a)!=gnu_dec32(&b)||a.counter!=b.counter)return 1;
  if(mini_inc32(&a)!=gnu_inc32(&b)||a.counter!=b.counter)return 2;
  atomic64_t c={v},d={v};
  if(mini_dec64(&c)!=gnu_dec64(&d)||c.counter!=d.counter)return 3;
  if(mini_inc64(&c)!=gnu_inc64(&d)||c.counter!=d.counter)return 4;
 }
 return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$work/atomic.i" -o "$work/atomic.s"
for op in 'lock; decl' 'lock; incl' 'lock; decq' 'lock; incq' 'sete %al'; do
 grep -Fq "$op" "$work/atomic.s"
done
cc -O0 -no-pie "$work/main.c" "$work/gnu.c" -x assembler "$work/atomic.s" -o "$work/oracle"
set +e
"$work/oracle"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL Linux atomic ZF GNU/MiniC differential $rc"; exit 1; }
cc -O0 -c -x assembler "$work/atomic.s" -o "$work/atomic.o"
objdump -h "$work/atomic.o" | grep -Fq '.smp_locks'
sed 's/lock; " "decl"/lock; " "decl; cli"/' "$work/atomic.i" > "$work/bad.i"
if "$minic" --target=x86_64-linux -S "$work/bad.i" -o "$work/bad.s" >/dev/null 2>&1; then
 echo 'FAIL privileged suffix accepted after Linux atomic'; exit 1
fi
echo 'MINIC_X86_LINUX_ATOMIC_ZF_GNU_NATIVE=PASS forms=4 GNU_pairs=28 ELF_SMP_LOCKS=1 negative=1'
