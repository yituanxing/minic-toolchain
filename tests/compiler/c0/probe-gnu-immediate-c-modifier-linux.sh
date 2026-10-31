#!/bin/sh
# Original Linux BUG/WARN .pushsection + GNU %c symbolic/numeric immediate.
# This oracle checks GCC, native assembly/link, runtime, and strict negatives.
set -eu
: "${MINIC_CC:?point to the exact minic-cc}"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT HUP INT TERM
cat >"$d/numeric.i" <<'SRC'
void linux_bug_reloc(void) {
  asm volatile("1:\t.byte 0x0f, 0x0b\n"
               ".pushsection __bug_table,\"aw\"\n"
               "2:\t.long 1b - .\n"
               ".long %c0 - .\n"
               ".word %c1\n"
               ".word %c2\n"
               ".org 2b+%c3\n"
               ".popsection\n"
               "998:\n"
               ".pushsection .discard.reachable\n"
               ".long 998b\n"
               ".popsection\n"
               : : "i"("frozen-linux.c"), "i"(519), "i"(0), "i"(12));
}
int main(void) { return 42; }
SRC
cat >"$d/named.i" <<'SRC'
void linux_bug_named(void) {
  asm volatile("1:\t.byte 0x0f, 0x0b\n"
               ".pushsection __bug_table,\"aw\"\n"
               "2:\t.long 1b - .\n"
               ".long %c[path] - .\n"
               ".word %c[ln]\n"
               ".word %c[fl]\n"
               ".org 2b+%c[sz]\n"
               ".popsection\n"
               : : [path] "i"("real-linux.c"), [ln] "i"(519),
                   [fl] "i"(0), [sz] "i"(12));
}
int main(void) { return 42; }
SRC
for kind in numeric named; do
    gcc -static -x c "$d/$kind.i" -o "$d/$kind.gnu"
    "$MINIC_CC" --target=x86_64-linux -S "$d/$kind.i" -o "$d/$kind.mini.s"
    gcc -c -x assembler "$d/$kind.mini.s" -o "$d/$kind.mini.o"
    gcc -static -x assembler "$d/$kind.mini.s" -o "$d/$kind.mini"
    gcc -c -x c "$d/$kind.i" -o "$d/$kind.gnu.o"
    readelf -r "$d/$kind.mini.o" | grep -Eq 'R_X86_64_PC32|R_X86_64_32'
    readelf -r "$d/$kind.gnu.o" | grep -Eq 'R_X86_64_PC32|R_X86_64_32'
    section=__bug_table
    readelf -SW "$d/$kind.mini.o" | grep -F "$section" >/dev/null
    grep -F '.word 519' "$d/$kind.mini.s" >/dev/null
    set +e
    "$d/$kind.gnu" >"$d/$kind.gnu.out" 2>"$d/$kind.gnu.err"
    gnu_rc=$?
    "$d/$kind.mini" >"$d/$kind.mini.out" 2>"$d/$kind.mini.err"
    mini_rc=$?
    set -e
    if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ] ||
       ! cmp -s "$d/$kind.gnu.out" "$d/$kind.mini.out" ||
       ! cmp -s "$d/$kind.gnu.err" "$d/$kind.mini.err"; then
        echo "MINIC_GNU_C_IMMEDIATE=FAIL kind=$kind" >&2; exit 1
    fi
done
cat >"$d/negative.i" <<'SRC'
int main(void) {
    asm volatile(".word %q0\n" : : "i"(123));
    return 42;
}
SRC
if "$MINIC_CC" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" 2>"$d/negative.err"; then
    echo "MINIC_GNU_C_IMMEDIATE=FAIL unknown_modifier_accepted" >&2;exit 1
fi
test ! -e "$d/negative.s"
echo "MINIC_GNU_C_IMMEDIATE=PASS GNU_C_MODIFIER named_and_numeric=2 native_elf_link_exec=1 unqualified_rejected=1"
