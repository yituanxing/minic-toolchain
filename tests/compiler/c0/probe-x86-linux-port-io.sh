#!/usr/bin/env bash
# Exact original 6.6.x arch/x86/include/asm/shared/io.h opcode contracts.
set -Eeuo pipefail
minic=$1;test -n "$minic"
w=$(mktemp -d);trap 'rm -rf "$w"' EXIT
cat >"$w/io.i" <<'EOF'
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
void mini_outb(u8 v,u16 p){asm volatile("outb %b0, %w1" : : "a"(v),"Nd"(p));}
void mini_outw(u16 v,u16 p){asm volatile("outw %w0, %w1" : : "a"(v),"Nd"(p));}
void mini_outl(u32 v,u16 p){asm volatile("outl %0, %w1" : : "a"(v),"Nd"(p));}
u8 mini_inb(u16 p){u8 v;asm volatile("inb %w1, %b0":"=a"(v):"Nd"(p));return v;}
u16 mini_inw(u16 p){u16 v;asm volatile("inw %w1, %w0":"=a"(v):"Nd"(p));return v;}
u32 mini_inl(u16 p){u32 v;asm volatile("inl %w1, %0":"=a"(v):"Nd"(p));return v;}
EOF
sed 's/mini_/gnu_/g' "$w/io.i" >"$w/gnu.c"
cc -x c -O0 -c "$w/gnu.c" -o "$w/gnu.o"
"$minic" --target=x86_64-linux -S "$w/io.i" -o "$w/mini.s"
cc -x assembler -c "$w/mini.s" -o "$w/mini.o"
for pattern in 'outb %al, %dx' 'outw %ax, %dx' 'outl %eax, %dx' \
 'inb %dx, %al' 'inw %dx, %ax' 'inl %dx, %eax';do
 grep -Fq "$pattern" "$w/mini.s"
done
objdump -d "$w/gnu.o" >"$w/gnu.dump"
objdump -d "$w/mini.o" >"$w/mini.dump"
for op in out in;do
 grep -Eq "[[:space:]]$op[[:space:]]" "$w/gnu.dump"
 grep -Eq "[[:space:]]$op[[:space:]]" "$w/mini.dump"
done
sed 's/outb %b0, %w1/outb %b0, %w1; cli/' "$w/io.i" >"$w/evil.i"
sed 's/"Nd"(p)/"r"(p)/' "$w/io.i" >"$w/constraint.i"
sed 's/void mini_outb(u8 v,u16 p)/void mini_outb(u8 v,u32 p)/' \
 "$w/io.i" >"$w/width.i"
sed 's/asm volatile("outb/asm("outb/' "$w/io.i" >"$w/nonvolatile.i"
for bad in evil constraint width nonvolatile;do
 if "$minic" --target=x86_64-linux -S "$w/$bad.i" \
      -o "$w/$bad.s" >/dev/null 2>&1;then
   echo "FAIL unsafe privileged port I/O: $bad" >&2;exit 1
 fi
 test ! -e "$w/$bad.s"
done
echo 'MINIC_X86_LINUX_PORT_IO=PASS widths=3 directions=2 machine=6 negative=4'
