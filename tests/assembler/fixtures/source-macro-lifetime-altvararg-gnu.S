/* `%` expands one character longer than the invocation spelling. The final
 * empty vararg follows an explicit comma. Historically MiniAS sized the
 * concatenation from the original text and could overwrite its trailing NUL. */
	.text
	.altmacro
	.macro emit_values values:vararg
	/* GNU GAS currently keeps `%` text literal in a vararg, while MiniAS
	 * evaluates it. The body intentionally ignores that unrelated syntax
	 * difference; both assemblers still bind and concatenate the arguments. */
	.byte 0x5a
	.endm
	.globl macro_lifetime_entry
macro_lifetime_entry:
	emit_values %(1<<29),
	emit_values %(1<<29),%(1<<29),
	.noaltmacro
