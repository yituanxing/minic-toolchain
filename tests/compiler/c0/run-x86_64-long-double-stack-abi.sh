#!/bin/sh
# SysV x86-64 x87 stack arguments: validate odd/even spill counts and
# downstream stack parameters in both MiniC <-> GNU call directions.
# Reuses the existing native M1 binary; no new CI workflow or GCC fallback.
set -eu
: "${MINIC_CC:?set MINIC_CC to native minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP: x87 stack ABI non-x86 host"; exit 0;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Seventh, eighth, and ninth integer arguments cause one, two, and
# three eightbyte stack spills before a 16-byte-aligned long double.
# A trailing int detects stack slot placement after the padded x87 slot.
cat > "$work/mini_send.i" <<'EOF'
extern int gnu_receive_7(int,int,int,int,int,int,int,long double,int);
extern int gnu_receive_8(int,int,int,int,int,int,int,int,long double,int);
extern int gnu_receive_9(int,int,int,int,int,int,int,int,int,long double,int);
int main(void) {
    volatile double d = 42.5;
    long double x = (long double)d;
    if (gnu_receive_7(1,2,3,4,5,6,7,x,19) != 42) return 1;
    if (gnu_receive_8(1,2,3,4,5,6,7,8,x,20) != 42) return 2;
    if (gnu_receive_9(1,2,3,4,5,6,7,8,9,x,21) != 42) return 3;
    return 42;
}
EOF
cat > "$work/gnu_recv.c" <<'EOF'
__attribute__((noinline))
int gnu_receive_7(int a,int b,int c,int d,int e,int f,int g,
                  long double x,int tail) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7 &&
           x==42.5L && tail==19 ? 42 : 1;
}
__attribute__((noinline))
int gnu_receive_8(int a,int b,int c,int d,int e,int f,int g,int h,
                  long double x,int tail) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7 &&
           h==8 && x==42.5L && tail==20 ? 42 : 2;
}
__attribute__((noinline))
int gnu_receive_9(int a,int b,int c,int d,int e,int f,int g,int h,int i,
                  long double x,int tail) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7 &&
           h==8 && i==9 && x==42.5L && tail==21 ? 42 : 3;
}
EOF
cat > "$work/mini_recv.i" <<'EOF'
int mini_receive_7(int a,int b,int c,int d,int e,int f,int g,
                   long double x,int tail) {
    volatile long double value = x;
    if (a!=1 || b!=2 || c!=3 || d!=4 || e!=5 || f!=6 || g!=7 ||
        tail!=19) return 1;
    return (double)value==42.5 ? 42 : 2;
}
int mini_receive_8(int a,int b,int c,int d,int e,int f,int g,int h,
                   long double x,int tail) {
    volatile long double value = x;
    if (a!=1 || b!=2 || c!=3 || d!=4 || e!=5 || f!=6 || g!=7 ||
        h!=8 || tail!=20) return 3;
    return (double)value==42.5 ? 42 : 4;
}
int mini_receive_9(int a,int b,int c,int d,int e,int f,int g,int h,int i,
                   long double x,int tail) {
    volatile long double value = x;
    if (a!=1 || b!=2 || c!=3 || d!=4 || e!=5 || f!=6 || g!=7 ||
        h!=8 || i!=9 || tail!=21) return 5;
    return (double)value==42.5 ? 42 : 6;
}
EOF
cat > "$work/gnu_send.c" <<'EOF'
extern int mini_receive_7(int,int,int,int,int,int,int,long double,int);
extern int mini_receive_8(int,int,int,int,int,int,int,int,long double,int);
extern int mini_receive_9(int,int,int,int,int,int,int,int,int,long double,int);
int main(void) {
    if (mini_receive_7(1,2,3,4,5,6,7,42.5L,19) != 42) return 1;
    if (mini_receive_8(1,2,3,4,5,6,7,8,42.5L,20) != 42) return 2;
    if (mini_receive_9(1,2,3,4,5,6,7,8,9,42.5L,21) != 42) return 3;
    return 42;
}
EOF

run_42() {
    label=$1
    binary=$2
    set +e
    "$binary"
    rc=$?
    set -e
    if [ "$rc" -ne 42 ]; then
        echo "FAIL: x86 x87 long double stack ABI $label status=$rc expected=42" >&2
        exit 1
    fi
    echo "PASS: x86 x87 long double stack ABI $label GCC_oracle=42"
}

for direction in send recv; do
    # A GNU/GNU build of the exact same C fixture first validates the oracle.
    if [ "$direction" = send ]; then
        gnu_source="$work/gnu_recv.c"
    else
        gnu_source="$work/gnu_send.c"
    fi
    "$HOST_CC" -std=gnu11 -O0 -no-pie -x c "$work/mini_$direction.i" \
        "$gnu_source" -o "$work/$direction.gnu.bin"
    run_42 "$direction/GNU-control" "$work/$direction.gnu.bin"

    "$MINIC_CC" --target=x86_64-linux -S "$work/mini_$direction.i" \
        -o "$work/mini_$direction.s"
    "$HOST_CC" -c -x assembler "$work/mini_$direction.s" \
        -o "$work/mini_$direction.o"
    "$HOST_CC" -std=gnu11 -O0 -c "$gnu_source" \
        -o "$work/gnu_$direction.o"
    "$HOST_CC" -no-pie "$work/mini_$direction.o" "$work/gnu_$direction.o" \
        -o "$work/$direction.mixed.bin"
    run_42 "$direction/MiniC-GNU-mixed" "$work/$direction.mixed.bin"
done
echo "X86_LONG_DOUBLE_STACK_ABI=PASS directions=2 spill_prefixes=1,2,3 trailing_stack_scalar=1 GNU_reference=1"
