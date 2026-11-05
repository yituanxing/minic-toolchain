/* Collector failure before a complete `.macro` can be registered. */
	.text
	.macro invalid_unterminated value:req
	.byte \value
