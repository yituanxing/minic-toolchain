.text
.macro emit_nops count
  .rept \count
    nop
  .endr
.endm

.globl source_context_entry
source_context_entry:
  je source_context_far
.set source_context_span, . - source_context_entry

/* A dot-dependent conditional triggers SourceGraph replay when JE grows.
   The active arm crosses the Common conditional -> Source macro boundary. */
.if . - .
  .error "unreachable conditional arm"
.else
  .altmacro
  emit_nops %(128+128)
  .noaltmacro
.endif

source_context_far:
  ret
.data
.long source_context_span
.end

/* Both assemblers must ignore this entire suffix, even after replay. */
.text
.byte 0xcc
.globl source_context_unreachable
source_context_unreachable:
  nop
