/* Failure after the macro was fully allocated and its body expanded. */
	.text
	.macro invalid_body value:req
	.byte \value
	.error "intentional-macro-body-failure"
	.endm
	invalid_body 7
