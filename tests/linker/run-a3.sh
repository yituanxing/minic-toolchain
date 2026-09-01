#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
AR=${RISCV_AR:-riscv64-linux-gnu-ar}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
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

cat >"$work/got.s" <<'EOF'
.option pic
.text
.globl got_probe
.type got_probe, @function
got_probe:
  la t0, got_value
  ld a0, 0(t0)
  ret
.size got_probe, .-got_probe

.data
.align 3
.globl got_value
.type got_value, @object
got_value:
  .dword 42
.size got_value, 8
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
  call got_probe
  li t0, 42
  bne a0, t0, 1f
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
"$AS" -march=rv64gc -mabi=lp64d -o "$work/got.o" "$work/got.s"
"$READELF" -Wr "$work/got.o" >"$work/got.relocations"
grep -q 'R_RISCV_GOT_HI20' "$work/got.relocations"
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
  cp libmini_runtime.a runtime-archive.o
  test "$(head -c 7 runtime-archive.o)" = '!<arch>'
)

"$LD" -melf64lriscv -static -e _start -o "$work/reference" \
  "$work/start.o" "$work/bounds.o" "$work/got.o" "$work/probe.o" \
  "$work/runtime-archive.o"

"$MINILD" -melf64lriscv -static -e _start -o "$work/product" \
  "$work/start.o" "$work/bounds.o" "$work/got.o" "$work/probe.o" \
  "$work/runtime-archive.o"

set +e
"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A3_DIAG reference_rc=$reference_rc product_rc=$product_rc archive_magic=$(head -c 7 "$work/runtime-archive.o")"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A3=PASS regular-archive=PASS magic-archive-o=PASS ordinary-archive=PASS symbol-less-member=PASS lazy-selection=PASS array-bounds=PASS static-got=PASS malloc=PASS free=PASS qemu_rc=$product_rc"
