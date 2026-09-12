#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${MINIAS:?MINIAS is required}"
: "${MINILD:?MINILD is required}"

QEMU="${QEMU_RISCV64:-qemu-riscv64}"
READELF="${RISCV_READELF:-riscv64-linux-gnu-readelf}"
NM="${RISCV_NM:-riscv64-linux-gnu-nm}"
WORK="${BUILD_DIR:-build}/linker-gc-v0"

rm -rf "$WORK"
mkdir -p "$WORK"

cat >"$WORK/gc.c" <<'C'
extern void minic_dead_missing_symbol(void);

static void minic_dead_function(void)
{
    minic_dead_missing_symbol();
}

static int minic_live_helper(void)
{
    return 23;
}

int minic_live_function(void)
{
    return minic_live_helper();
}
C

cat >"$WORK/start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call minic_live_function
    li a7, 93
    ecall
.size _start, .-_start
S

"$MINIC" -ffreestanding -fno-builtin -nostdinc \
  -c "$WORK/gc.c" -o "$WORK/gc.o"
"$MINIAS" -march=rv64gc -mabi=lp64d \
  -o "$WORK/start.o" "$WORK/start.s"

"$READELF" -SW "$WORK/gc.o" >"$WORK/gc.sections"
grep -q '\.text\.minic_dead_function' "$WORK/gc.sections"
grep -q '\.text\.minic_live_helper' "$WORK/gc.sections"
grep -q '\.text\.minic_live_function' "$WORK/gc.sections"

# Without GC, the dead function's undefined edge must remain observable.
set +e
"$MINILD" -melf64lriscv -static -e _start \
  -o "$WORK/no-gc.elf" "$WORK/start.o" "$WORK/gc.o" \
  >"$WORK/no-gc.stdout" 2>"$WORK/no-gc.stderr"
no_gc_rc=$?
set -e
test "$no_gc_rc" -ne 0
grep -q 'minic_dead_missing_symbol' "$WORK/no-gc.stderr"

# With GC, _start -> live_function -> live_helper stays reachable while the
# dead function and its undefined relocation are discarded.
"$MINILD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/gc.elf" "$WORK/start.o" "$WORK/gc.o" \
  >"$WORK/gc.stdout" 2>"$WORK/gc.stderr"

test -s "$WORK/gc.elf"
grep -q 'gc-sections:live=' "$WORK/gc.stderr"
set +e
"$QEMU" "$WORK/gc.elf"
qemu_rc=$?
set -e
test "$qemu_rc" -eq 23

"$NM" "$WORK/gc.elf" >"$WORK/gc.symbols"
if grep -Eq ' [Tt] minic_dead_function$' "$WORK/gc.symbols"; then
  echo "MINILD_GC_ERROR dead function survived" >&2
  exit 1
fi
grep -Eq ' [Tt] minic_live_function$' "$WORK/gc.symbols"
grep -Eq ' [Tt] minic_live_helper$' "$WORK/gc.symbols"

echo "MINILD_GC_FUNCTION_SECTIONS=PASS dead=discarded transitive=retained qemu_rc=23"

# Real static archives (musl/libgcc) commonly use the same plain `.text` name
# in many different input objects.  GC semantics are defined over input
# sections, not over an output section produced by eagerly concatenating every
# same-named input section.  Freeze that distinction explicitly.
cat >"$WORK/plain-start.s" <<'S'
.text
.globl _start
.type _start, @function
_start:
    call plain_live
    li a7, 93
    ecall
.size _start, .-_start
S

cat >"$WORK/plain-live.s" <<'S'
.text
.globl plain_live
.type plain_live, @function
plain_live:
    li a0, 31
    ret
.size plain_live, .-plain_live
S

cat >"$WORK/plain-dead.s" <<'S'
.text
.globl plain_dead
.type plain_dead, @function
plain_dead:
    call plain_dead_missing_symbol
    ret
.size plain_dead, .-plain_dead
S

"$MINIAS" -march=rv64gc -mabi=lp64d -o "$WORK/plain-start.o" "$WORK/plain-start.s"
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$WORK/plain-live.o" "$WORK/plain-live.s"
"$MINIAS" -march=rv64gc -mabi=lp64d -o "$WORK/plain-dead.o" "$WORK/plain-dead.s"

"$MINILD" -melf64lriscv -static --gc-sections -e _start \
  -o "$WORK/plain-gc.elf" \
  "$WORK/plain-start.o" "$WORK/plain-live.o" "$WORK/plain-dead.o" \
  >"$WORK/plain-gc.stdout" 2>"$WORK/plain-gc.stderr"

test -s "$WORK/plain-gc.elf"
set +e
"$QEMU" "$WORK/plain-gc.elf"
plain_rc=$?
set -e
test "$plain_rc" -eq 31
"$NM" "$WORK/plain-gc.elf" >"$WORK/plain-gc.symbols"
if grep -Eq ' [Tt] plain_dead$' "$WORK/plain-gc.symbols"; then
  echo "MINILD_GC_ERROR duplicate-name dead input section survived" >&2
  exit 1
fi
grep -Eq ' [Tt] plain_live$' "$WORK/plain-gc.symbols"

echo "MINILD_GC_INPUT_IDENTITY=PASS duplicate_name=.text qemu_rc=31"
echo "MINILD_GC_V0=PASS function_sections=PASS input_identity=PASS"
