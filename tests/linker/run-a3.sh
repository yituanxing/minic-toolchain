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

cat >"$work/bounds.s" <<'EOF'
.text
.globl check_array_bounds
.type check_array_bounds, @function
check_array_bounds:
  la t0, __init_array_start
  la t1, __init_array_end
  sub t1, t1, t0
  li t2, 8
  bne t1, t2, 1f

  la t0, __preinit_array_start
  la t1, __preinit_array_end
  bne t0, t1, 1f

  la t0, __fini_array_start
  la t1, __fini_array_end
  bne t0, t1, 1f

  li a0, 0
  ret
1:
  li a0, 99
  ret
.size check_array_bounds, .-check_array_bounds

.section .init_array.0100,"aw",@progbits
.align 3
.dword runtime_init_stub

.text
.type runtime_init_stub, @function
runtime_init_stub:
  ret
.size runtime_init_stub, .-runtime_init_stub
EOF

cat >"$work/probe.s" <<'EOF'
.text
.globl runtime_malloc_free_probe
.type runtime_malloc_free_probe, @function
runtime_malloc_free_probe:
  addi sp, sp, -16
  sd ra, 8(sp)
  call check_array_bounds
  bnez a0, 1f
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
"$AS" -march=rv64gc -mabi=lp64d -o "$work/bounds.o" "$work/bounds.s"
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
  "$work/start.o" "$work/bounds.o" "$work/probe.o" \
  "$work/libmini_runtime.a"

"$MINILD" -melf64lriscv -static -e _start -o "$work/product" \
  "$work/start.o" "$work/bounds.o" "$work/probe.o" \
  "$work/libmini_runtime.a"

set +e
"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A3_DIAG reference_rc=$reference_rc product_rc=$product_rc archive_magic=$(head -c 7 "$work/libmini_runtime.a")"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A3=PASS regular-archive=PASS ordinary-archive=PASS symbol-less-member=PASS lazy-selection=PASS array-bounds=PASS malloc=PASS free=PASS qemu_rc=$product_rc"
