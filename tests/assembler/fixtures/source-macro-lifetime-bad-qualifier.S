/* Failure after a macro name and the first parameter have been allocated. */
	.text
	.macro invalid_qualifier first:req, second:invalid
	.byte \first
	.endm
	invalid_qualifier 1, 2
