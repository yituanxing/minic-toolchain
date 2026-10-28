#!/usr/bin/env bash
# Original Linux 6.6.143 arch/x86/include/asm/local.h exact local_t forms.
set -Eeuo pipefail
cc=${1:?MiniC compiler}
test -x "$cc"
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat > "$d/local.i" <<'SRC'
typedef struct { long counter; } local_t;
void mini_inc(local_t *l) { asm volatile(" incq %0" : "+m"(l->counter)); }
void mini_dec(local_t *l) { asm volatile(" decq %0" : "+m"(l->counter)); }
void mini_add(long v, local_t *l) { asm volatile(" addq %1,%0" : "+m"(l->counter) : "ir"(v)); }
void mini_sub(long v, local_t *l) { asm volatile(" subq %1,%0" : "+m"(l->counter) : "ir"(v)); }
void mini_inc32(int *v) { asm volatile(" incl %0" : "+m"(*v)); }
void mini_dec32(int *v) { asm volatile(" decl %0" : "+m"(*v)); }
SRC
"$cc" --target=x86_64-linux -S "$d/local.i" -o "$d/mini.s"
for ins in incq decq addq subq incl decl; do
    grep -Eq "^  $ins .*\\(%r11\\)" "$d/mini.s"
done
if grep -Eq '^  lock; (incq|decq|addq|subq|incl|decl) ' "$d/mini.s"; then
  echo 'FAIL: local non-atomic RMW acquired SMP LOCK' >&2; exit 1
fi
sed 's/mini_/gnu_/g' "$d/local.i" > "$d/gnu.c"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
cat > "$d/main.c" <<'SRC'
typedef struct { long counter; } local_t;
extern void mini_inc(local_t*),mini_dec(local_t*);
extern void mini_add(long,local_t*),mini_sub(long,local_t*);
extern void gnu_inc(local_t*),gnu_dec(local_t*);
extern void gnu_add(long,local_t*),gnu_sub(long,local_t*);
extern void mini_inc32(int*),mini_dec32(int*);
extern void gnu_inc32(int*),gnu_dec32(int*);
int main(void) {
  long seeds[]={0,1,-1,0x123456789L,-0x123456789L};
  for(unsigned i=0;i<sizeof(seeds)/sizeof(seeds[0]);++i) {
    local_t a={seeds[i]},b={seeds[i]};
    mini_inc(&a); gnu_inc(&b); if(a.counter!=b.counter)return 1;
    mini_dec(&a); gnu_dec(&b); if(a.counter!=b.counter)return 2;
    mini_add(17,&a); gnu_add(17,&b); if(a.counter!=b.counter)return 3;
    mini_sub(-11,&a); gnu_sub(-11,&b); if(a.counter!=b.counter)return 4;
    int c=(int)seeds[i],d=(int)seeds[i];
    mini_inc32(&c);gnu_inc32(&d);if(c!=d)return 5;
    mini_dec32(&c);gnu_dec32(&d);if(c!=d)return 6;
  }
  return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/gnu.o" "$d/mini.o" -o "$d/run"
"$d/run"
for bad in not-volatile wrong-type extra-instruction; do
  case "$bad" in
    not-volatile) sed 's/asm volatile/asm/g' "$d/local.i" > "$d/bad.i" ;;
    wrong-type) sed 's/long counter;/double counter;/' "$d/local.i" > "$d/bad.i" ;;
    extra-instruction) sed 's/incq %0/incq %0; nop/' "$d/local.i" > "$d/bad.i" ;;
  esac
  if "$cc" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1;then
    echo "FAIL: unsafe local RMW $bad accepted" >&2; exit 1
  fi
  test ! -e "$d/bad.s"
done
echo 'MINIC_X86_LINUX_LOCAL_RMW=PASS genuine_linux=1 six_native_forms=1 GNU_runtime=1 negatives=3'
