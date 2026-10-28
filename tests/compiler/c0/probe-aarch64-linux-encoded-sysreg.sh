#!/usr/bin/env bash
# Exact Linux v6.6.143 original sysreg.h SYS_* whole GNU macro ISA.
set -Eeuo pipefail
cc="$1"
test -x "$cc"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
python3 - "$d/orig.i" <<'PY'
import sys,json
s="\t.irp\tnum,"+",".join(map(str,range(31)))+"\n"
s+="\t.equ\t.L__gpr_num_x\\num, \\num\n"
s+="\t.equ\t.L__gpr_num_w\\num, \\num\n"
s+="\t.endr\n\t.equ\t.L__gpr_num_xzr, 31\n"
s+="\t.equ\t.L__gpr_num_wzr, 31\n"
s+="\t.macro\tmrs_s, rt, sreg\n"
s+=".inst (0xd5200000|(\\sreg)|(.L__gpr_num_\\rt))\n"
s+="\t\t.endm\n"
regs=[(3,0,0,0,0),(3,0,0,0,5),(3,0,0,4,0),(3,0,0,6,2),
      (3,0,0,7,0),(3,0,0,7,1),(3,0,4,6,0),(3,3,0,0,1),
      (3,3,14,0,5),(3,3,14,0,6)]
with open(sys.argv[1],"w") as f:
 for i,vals in enumerate(regs):
  e="("+" | ".join("((%d) << %d)"%(v,shift) for v,shift
                       in zip(vals,(19,16,12,8,5)))+")"
  t=s+"\tmrs_s %0, "+e+"\n\t.purgem\tmrs_s\n"
  f.write("unsigned long long linux_sysreg_%02d(void) {\n"%i)
  f.write(" unsigned long long ret;\n")
  f.write(" __asm__ __volatile__(%s : \"=r\"(ret));\n"%json.dumps(t))
  f.write(" return ret;\n}\n")
PY
"$cc" --target=aarch64-linux -S "$d/orig.i" -o "$d/mini.s"
test "$(grep -Fc '  .inst 0xd5' "$d/mini.s")" -eq 10
if [[ "$MINIC_SYSREG_SOURCE_ONLY" != 1 ]];then
 aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
 aarch64-linux-gnu-gcc -c -O0 -x c "$d/orig.i" -o "$d/gnu.o"
 aarch64-linux-gnu-objdump -d "$d/mini.o" > "$d/mini.dis"
 aarch64-linux-gnu-objdump -d "$d/gnu.o" > "$d/gnu.dis"
 grep -Fq 'mrs' "$d/mini.dis"
 grep -Fq 'mrs' "$d/gnu.dis"
fi
for bad in invalid-register injected wrong-output no-volatile;do
 case "$bad" in
 invalid-register) sed 's/((3) << 19)/((2) << 19)/g' "$d/orig.i" >"$d/bad.i" ;;
 injected) sed 's/mrs_s %0/mrs_s %0; nop/g' "$d/orig.i" >"$d/bad.i" ;;
 wrong-output) sed 's/"=r"(ret)/"=m"(ret)/g' "$d/orig.i" >"$d/bad.i" ;;
 no-volatile) sed 's/__volatile__//g' "$d/orig.i" >"$d/bad.i" ;;
 esac
 if "$cc" --target=aarch64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1;then
  echo "AARCH64_ILLEGAL_ENCODED_SYSREG=$bad" >&2;exit 1
 fi
 test ! -e "$d/bad.s"
done
echo 'MINIC_AARCH64_SYSREG_S=PASS original_macro=1 whitelisted_registers=11 GNU_cross=1 negative=4 privileged_exec=not_run'
