#!/usr/bin/env bash
# ISA FP register/rounding operands are typed by the RV Target, not guessed
# from Common integer-register fields or accepted outside -march/-mabi.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"

gnu_available=1
if ! command -v riscv64-linux-gnu-as >/dev/null ||
   ! command -v riscv64-linux-gnu-objcopy >/dev/null; then
  if [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
    gnu_available=0
  else
    echo 'GNU RV assembler and objcopy are required by this acceptance gate' >&2
    exit 1
  fi
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_RV_TYPED_FP=FAIL command=$BASH_COMMAND" >&2' ERR

cat >"$work/rv32.s" <<'ASM'
.text
  fadd.s f0,f1,f2
  fadd.s f3,f4,f5,rne
  flw f0,0(a0)
  fsw f0,4(a0)
  fmv.x.w a0,f1
ASM
cat >"$work/rv64.s" <<'ASM'
.text
  fadd.d f0,f1,f2,rne
  fsgnjn.d f0,f1,f2
  feq.d a0,f0,f1
  fmv.x.d a0,f1
  fmv.d.x f1,a0
  fcvt.d.l f0,a0
  fcvt.l.d a0,f0,rtz
  fcvt.s.d f0,f1
  fld f0,0(a0)
  fsd f0,8(a0)
ASM

for arch in rv32 rv64; do
  if [[ "$arch" == rv32 ]]; then
    march=rv32if; mabi=ilp32f; elfclass=ELF32
  else
    march=rv64ifd; mabi=lp64d; elfclass=ELF64
  fi
  "$MINIAS" --target=riscv -march="$march" -mabi="$mabi" \
    -o "$work/$arch.mini.o" "$work/$arch.s"
  readelf -h "$work/$arch.mini.o" | grep -q "Class:.*$elfclass"
  if ((gnu_available)); then
    riscv64-linux-gnu-as -march="$march" -mabi="$mabi" \
      -o "$work/$arch.gnu.o" "$work/$arch.s"
    for tool in mini gnu; do
      riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/$arch.$tool.o" "$work/$arch.$tool.bin"
    done
    cmp "$work/$arch.mini.bin" "$work/$arch.gnu.bin"
  fi
done

reject() {
  local name=$1 march=$2 mabi=$3 statement=$4
  printf '.text\n%s\n' "$statement" >"$work/$name.s"
  if "$MINIAS" --target=riscv -march="$march" -mabi="$mabi" \
    -o "$work/$name.o" "$work/$name.s" 2>"$work/$name.err"; then
    echo "RISC-V typed FP accepted invalid form: $name $statement" >&2
    exit 1
  fi
  test ! -e "$work/$name.o"
}
reject no_d_add rv64if lp64f 'fadd.d f0,f1,f2'
reject no_d_load rv64if lp64f 'fld f0,0(a0)'
reject no_d_convert rv64if lp64f 'fcvt.s.d f0,f1'
reject rv32_fmv_xd rv32ifd ilp32d 'fmv.x.d a0,f1'
reject rv32_fmv_dx rv32ifd ilp32d 'fmv.d.x f1,a0'
reject rv32_fcvt_dl rv32ifd ilp32d 'fcvt.d.l f0,a0'
reject rv32_fcvt_ld rv32ifd ilp32d 'fcvt.l.d a0,f0'
reject no_f rv32i ilp32 'fadd.s f0,f1,f2'
reject bad_round rv64ifd lp64d 'fadd.s f0,f1,f2,invalidrm'
reject raw_i_escape rv32i ilp32 '.insn 0x00000053'

if ((gnu_available)); then oracle=exact; else oracle='NOT_TESTED:explicit_local_no_cross_GNU'; fi
echo "MINIAS_RV_TYPED_FP=PASS rv32if=5 rv64ifd=10 GNU_bytes=$oracle rejected=10 no_partial_object=1"
