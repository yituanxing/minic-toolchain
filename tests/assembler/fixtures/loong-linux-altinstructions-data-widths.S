/* Linux-style alternative instruction metadata: .byte differences must
 * retain relocatable ADD8/SUB8 pairs across `.pushsection` and `.subsection`.
 * A 1-byte data fixup is no less a Fragment-owned transaction than .long. */
.text
.globl minias_loong_alt_widths
.type minias_loong_alt_widths, @function
minias_loong_alt_widths:
140:
  nop
141:
  .fill 1, 4, 0x03400000
142:
  .pushsection .altinstructions, "a"
  .long 140b - .
  .long 143f - .
  .short 2
  .byte 142b - 140b
  .byte 144f - 143f
  .popsection
  .subsection 1
143:
  nop
144:
  .previous
.size minias_loong_alt_widths, .-minias_loong_alt_widths
