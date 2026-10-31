#!/usr/bin/env bash
# Original x86 vDSO fallback syscall forms: 5-operand clock and 4-operand
# gettimeofday. Real Linux syscalls exercise register ABI and =m writes.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
struct KernelTs { long sec; long nsec; };
struct OldTv { long sec; long usec; };
struct Timezone { int minutes; int dst; };
long mini_gettime(int id, struct KernelTs *ts) {
    long ret;
    asm("syscall" : "=a"(ret), "=m"(*ts)
        : "0"(228), "D"(id), "S"(ts) : "rcx", "r11");
    return ret;
}
long mini_getres(int id, struct KernelTs *ts) {
    long ret;
    asm("syscall" : "=a"(ret), "=m"(*ts)
        : "0"(229), "D"(id), "S"(ts) : "rcx", "r11");
    return ret;
}
long mini_gettimeofday(struct OldTv *tv,struct Timezone *tz) {
    long ret;
    asm("syscall" : "=a"(ret)
        : "0"(96), "D"(tv), "S"(tz) : "memory");
    return ret;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
struct KernelTs { long sec; long nsec; };
struct OldTv { long sec; long usec; };
struct Timezone { int minutes; int dst; };
extern long mini_gettime(int,struct KernelTs*),gnu_gettime(int,struct KernelTs*);
extern long mini_getres(int,struct KernelTs*),gnu_getres(int,struct KernelTs*);
extern long mini_gettimeofday(struct OldTv*,struct Timezone*);
extern long gnu_gettimeofday(struct OldTv*,struct Timezone*);
int main(void) {
    for (int n=0;n<16;n++) {
        struct KernelTs a={0},b={0},c={0},d={0};
        struct OldTv tv={0},gv={0};
        struct Timezone tz={0},gtz={0};
        int id=n&1;
        long ra=mini_gettime(id,&a),rb=gnu_gettime(id,&b);
        if(ra!=rb||ra!=0||a.sec-b.sec>1||b.sec-a.sec>1||
           a.nsec<0||a.nsec>=1000000000L||
           b.nsec<0||b.nsec>=1000000000L) return 1;
        ra=mini_getres(id,&c);rb=gnu_getres(id,&d);
        if(ra!=rb||ra!=0||c.sec!=d.sec||c.nsec!=d.nsec) return 2;
        ra=mini_gettimeofday(&tv,&tz);rb=gnu_gettimeofday(&gv,&gtz);
        if(ra!=rb||ra!=0||tv.sec-gv.sec>1||gv.sec-tv.sec>1||
           tv.usec<0||tv.usec>=1000000L||
           gv.usec<0||gv.usec>=1000000L||
           tz.minutes!=gtz.minutes||tz.dst!=gtz.dst) return 3;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/asm("syscall"/asm("syscall; cli"/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_VDSO_SYSCALL_FAIL=unadmitted opcode suffix' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_VDSO_SYSCALL_NATIVE=PASS gettime=16 getres=16 gettimeofday=16 negative=1'
