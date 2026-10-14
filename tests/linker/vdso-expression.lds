OUTPUT_ARCH(riscv)
SECTIONS {
  PROVIDE(_vdso_data = . - 2 * (1 << 12));
  PROVIDE(_timens_data = _vdso_data + (1 << 12));
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :text
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
