	.text
	.p2align 2
	.global weak_literal
	.type weak_literal, %object
weak_literal:
	.xword 0xaabbccddeeff0011
	.global external_literal
	.type external_literal, %object
external_literal:
	.word 0x99bbddff
