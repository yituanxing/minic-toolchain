#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
AR=${RISCV_AR:-riscv64-linux-gnu-ar}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
QEMU=${QEMU_RISCV64:-qemu-riscv64}

work="$BUILD_DIR/tests/linker/a3"
rm -rf "$work"
mkdir -p "$work/runtime"

cat >"$work/start.s" <<'EOF'
.section .text.start,"ax",@progbits
.globl _start
.type _start, @function
_start:
  call runtime_malloc_free_probe
  li a7, 93
  ecall
.size _start, .-_start
EOF

cat >"$work/probe.s" <<'EOF'
.text
.globl runtime_malloc_free_probe
.type runtime_malloc_free_probe, @function
runtime_malloc_free_probe:
  addi sp, sp, -16
  sd ra, 8(sp)
  li a0, 32
  call malloc
  beqz a0, 1f
  mv t0, a0
  li t1, 40
  sd t1, 0(t0)
  ld t2, 0(t0)
  mv a0, t0
  call free
  addi a0, t2, 2
  j 2f
1:
  li a0, 1
2:
  ld ra, 8(sp)
  addi sp, sp, 16
  ret
.size runtime_malloc_free_probe, .-runtime_malloc_free_probe
EOF

cat >"$work/runtime/runtime_malloc_allocator_member.s" <<'EOF'
.bss
.align 4
runtime_heap:
  .zero 4096

.text
.globl malloc
.type malloc, @function
malloc:
  la a0, runtime_heap
  ret
.size malloc, .-malloc
EOF

cat >"$work/runtime/runtime_free_coalesce_member.s" <<'EOF'
.text
.globl free
.type free, @function
free:
  ret
.size free, .-free
EOF

cat >"$work/runtime/symbol_less_member.s" <<'EOF'
.section .note.minild.a3,"",@progbits
.byte 1,2,3,4
EOF

cat >"$work/runtime/unused_runtime_member_with_bad_reference.s" <<'EOF'
.text
.globl unused_runtime_member
.type unused_runtime_member, @function
unused_runtime_member:
  call symbol_that_must_remain_unresolved
  ret
.size unused_runtime_member, .-unused_runtime_member
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/start.o" "$work/start.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/probe.o" "$work/probe.s"
"$AS" -march=rv64gc -mabi=lp64d \
  -o "$work/runtime/runtime_malloc_allocator_member.o" \
  "$work/runtime/runtime_malloc_allocator_member.s"
"$AS" -march=rv64gc -mabi=lp64d \
  -o "$work/runtime/runtime_free_coalesce_member.o" \
  "$work/runtime/runtime_free_coalesce_member.s"
"$AS" -march=rv64gc -mabi=lp64d \
  -o "$work/runtime/symbol_less_member.unstripped.o" \
  "$work/runtime/symbol_less_member.s"
"$OBJCOPY" --strip-all \
  "$work/runtime/symbol_less_member.unstripped.o" \
  "$work/runtime/symbol_less_member.o"
"$AS" -march=rv64gc -mabi=lp64d \
  -o "$work/runtime/unused_runtime_member_with_bad_reference.o" \
  "$work/runtime/unused_runtime_member_with_bad_reference.s"

(
  cd "$work"
  "$AR" rcsD libmini_runtime.a \
    runtime/runtime_malloc_allocator_member.o \
    runtime/runtime_free_coalesce_member.o \
    runtime/symbol_less_member.o \
    runtime/unused_runtime_member_with_bad_reference.o
  test "$(head -c 7 libmini_runtime.a)" = '!<arch>'
)

"$LD" -melf64lriscv -static -e _start -o "$work/reference" \
  "$work/start.o" "$work/probe.o" \
  --start-group "$work/libmini_runtime.a" --end-group

"$MINILD" -melf64lriscv -static -e _start -o "$work/product" \
  "$work/start.o" "$work/probe.o" \
  --start-group "$work/libmini_runtime.a" --end-group

set +e
"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A3_DIAG reference_rc=$reference_rc product_rc=$product_rc archive_magic=$(head -c 7 "$work/libmini_runtime.a")"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A3=PASS regular-archive=PASS symbol-less-member=PASS lazy-selection=PASS malloc=PASS free=PASS qemu_rc=$product_rc"
