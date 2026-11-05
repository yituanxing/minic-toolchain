OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  .rela.dyn : { *(.rela.dyn) }
  /* Isolate the dynamic-relocation contract from unrelated orphan-section
   * placement in ordinary GNU shared-library emulations. */
  . = 0x401000;
  .data : { *(.data.alive) }
  /DISCARD/ : {
    *(.data.discard)
    /* Match Linux's kernel shared-image policy: dynamic symbol tables and
     * loader sections are not part of the final executable image. Keeping
     * GNU's ordinary shared-library orphans shifts .data and thus r_offset
     * even though the BFD and MiniLD relocation semantics already agree. */
    *(.interp .dynamic)
    *(.dynsym .dynstr .hash .gnu.hash)
    *(.gnu.version*)
  }
}
