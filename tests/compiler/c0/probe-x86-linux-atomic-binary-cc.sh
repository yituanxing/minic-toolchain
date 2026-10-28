#!/usr/bin/env bash
set -Eeuo pipefail
minic=$1; dir=$(mktemp -d); trap 'rm -rf "$dir"' EXIT
cat >"$dir/atomic.i" <<'EOF'
typedef _Bool bool;
typedef struct { int counter; } atomic_t;
typedef struct { long long counter; } atomic64_t;
bool mini_sub32(int n, atomic_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
 ".long 671f - .\n" ".popsection\n" "671:"
 "\n\tlock; subl %[val], %[var]\n\t/* output condition code e*/\n"
 : [var] "+m"(v->counter), "=@cce"(c) : [val] "er"(n) : "memory");
 return c;
}
bool mini_add32(int n, atomic_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
 ".long 671f - .\n" ".popsection\n" "671:"
 "\n\tlock; addl %[val], %[var]\n\t/* output condition code s*/\n"
 : [var] "+m"(v->counter), "=@ccs"(c) : [val] "er"(n) : "memory");
 return c;
}
bool mini_sub64(long long n, atomic64_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
 ".long 671f - .\n" ".popsection\n" "671:"
 "\n\tlock; subq %[val], %[var]\n\t/* output condition code e*/\n"
 : [var] "+m"(v->counter), "=@cce"(c) : [val] "er"(n) : "memory");
 return c;
}
bool mini_add64(long long n, atomic64_t *v) {
 bool c;
 asm volatile(".pushsection .smp_locks,\"a\"\n" ".balign 4\n"
 ".long 671f - .\n" ".popsection\n" "671:"
 "\n\tlock; addq %[val], %[var]\n\t/* output condition code s*/\n"
 : [var] "+m"(v->counter), "=@ccs"(c) : [val] "er"(n) : "memory");
 return c;
}
EOF
sed 's/mini_/gcc_/g' "$dir/atomic.i" >"$dir/oracle.c"
cat >"$dir/main.c" <<'EOF'
typedef struct {int counter;} atomic_t;
typedef struct {long long counter;} atomic64_t;
extern _Bool mini_sub32(int,atomic_t*),gcc_sub32(int,atomic_t*);
extern _Bool mini_add32(int,atomic_t*),gcc_add32(int,atomic_t*);
extern _Bool mini_sub64(long long,atomic64_t*),gcc_sub64(long long,atomic64_t*);
extern _Bool mini_add64(long long,atomic64_t*),gcc_add64(long long,atomic64_t*);
int main(void) {
  const int values[]={-9,-3,-1,0,1,3,9};
  int i,j;
  for (i=0;i<7;++i) for(j=0;j<7;++j){
    atomic_t a={values[i]},b={values[i]};
    atomic64_t c={values[i]},d={values[i]};
    int n=values[j];
    if (mini_sub32(n,&a)!=gcc_sub32(n,&b)||a.counter!=b.counter)return 1;
    a.counter=b.counter=values[i];
    if (mini_add32(n,&a)!=gcc_add32(n,&b)||a.counter!=b.counter)return 2;
    if (mini_sub64(n,&c)!=gcc_sub64(n,&d)||c.counter!=d.counter)return 3;
    c.counter=d.counter=values[i];
    if (mini_add64(n,&c)!=gcc_add64(n,&d)||c.counter!=d.counter)return 4;
  }
  return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$dir/atomic.i" -o "$dir/atomic.s"
for instr in 'lock; subl' 'lock; addl' 'lock; subq' 'lock; addq' 'sete %al' 'sets %al'; do
  grep -Fq "$instr" "$dir/atomic.s"
done
cc -O0 -no-pie "$dir/main.c" "$dir/oracle.c" -x assembler "$dir/atomic.s" -o "$dir/diff"
set +e
"$dir/diff"; code=$?
set -e
test "$code" -eq 42 || { echo "FAIL x86 binary RMWcc GCC rc=$code"; exit 1; }
cc -c -x assembler "$dir/atomic.s" -o "$dir/atomic.o"
objdump -h "$dir/atomic.o" | grep -Fq '.smp_locks'
sed 's/=@ccs/=@ccc/g' "$dir/atomic.i" > "$dir/bad.i"
if "$minic" --target=x86_64-linux -S "$dir/bad.i" -o "$dir/bad.s" >"$dir/bad.log" 2>&1; then
  echo 'FAIL x86 RMWcc flags mismatch accepted'; exit 1
fi
sed 's/lock; subl/lock; subl; cli/' "$dir/atomic.i" > "$dir/inject.i"
if "$minic" --target=x86_64-linux -S "$dir/inject.i" -o "$dir/inject.s" >"$dir/inject.log" 2>&1; then
  echo 'FAIL x86 RMWcc privileged suffix accepted'; exit 1
fi
echo 'MINIC_X86_LINUX_BINARY_RMWCC=PASS modes=4 value_pairs=196 ELF_SMP_LOCKS=1 negative=2'
