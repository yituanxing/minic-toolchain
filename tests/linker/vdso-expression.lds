OUTPUT_ARCH(riscv)
SECTIONS {
  PROVIDE(_vdso_data = . - 2 * (1 << 12));
  PROVIDE(_timens_data = _vdso_data + (1 << 12));
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) }
}
