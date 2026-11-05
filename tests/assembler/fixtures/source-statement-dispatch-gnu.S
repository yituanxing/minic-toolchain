.text
.macro SOURCE_DISPATCH_BYTE value
    .byte \value
.endm

.globl source_dispatch_entry
source_dispatch_entry:
first_label: second_label: SOURCE_DISPATCH_BYTE 0x11
spaced_label : 0: .byte 0x12
    .byte 0b - 0b
if_label_a: if_label_b: .if 1
    .byte 0x22
.else
    .byte 0x99
.endif

.if 0
hidden_a: hidden_b: SOURCE_DISPATCH_BYTE 0xff
.else
    .byte 0x23
.endif

rept_a: rept_b: .rept 2
    .byte 0x33
.endr
irp_a: irp_b: .irp value,0x44,0x55
    .byte \value
.endr

string_anchor: .asciz "target:colon;is quoted"
alt_anchor: .altmacro
    SOURCE_DISPATCH_BYTE %(0x60+6)
noalt_anchor: .noaltmacro
purge_anchor: .purgem SOURCE_DISPATCH_BYTE
end_anchor: .end

.byte 0xfe
hidden_after_end:
    .byte 0xfd
