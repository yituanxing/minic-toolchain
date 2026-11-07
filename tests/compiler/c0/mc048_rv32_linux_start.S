.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  call main
  li a7, 93
  ecall
