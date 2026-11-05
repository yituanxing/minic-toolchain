/* LoongArch Linux 6.6.143's preprocessed vmlinux.lds has the first symbol
 * assignment below verbatim. The remaining expressions check GNU operator
 * precedence and bounded 64-bit evaluation independently of any target. */
OUTPUT_ARCH(loongarch)
ENTRY(kernel_entry)
SECTIONS {
  . = 0x10000;
  _kernel_entry = ABSOLUTE(kernel_entry & 0xffffffffffff);
  _precedence = 1 | 2 & 4;
  _xor = 1 ^ 3 & 2;
  _shift = 8 >> 1 + 1;
  _modulo = 44 % 7;
  _complement = ~0xffffffffffff & 0xffffffffffff;
  _invalid_shift = 1 >> 64;
  _invalid_modulo = 5 % 0;
  .text : { *(.text) }
}
