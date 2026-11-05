	.text
	.p2align 2
	.global ldr_binding_entry
	.type ldr_binding_entry, %function
ldr_binding_entry:
	ldr x0, .Llocal_literal
	ldr x1, global_literal
	ldr x2, weak_literal
	ldr x3, hidden_literal+8
	ldr w4, external_literal
	ldr x6, =global_literal
	ret
	.type .Llocal_literal, %object
.Llocal_literal:
	.xword 0x1122334455667788
	.global global_literal
	.type global_literal, %object
global_literal:
	.xword 0x8877665544332211
	.weak weak_literal
	.type weak_literal, %object
weak_literal:
	.xword 0x1234567812345678
	.global hidden_literal
	.hidden hidden_literal
	.type hidden_literal, %object
hidden_literal:
	.xword 0x55aa55aa55aa55aa
	.ltorg
