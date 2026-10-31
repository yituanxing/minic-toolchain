	.text
	.p2align 2
	.global branch_entry
	.type branch_entry, %function
branch_entry:
	bl local_target
	b local_target
	bl global_target
	b global_target
	bl weak_target
	b weak_target
	bl hidden_target
	b hidden_target
	cbz w0, global_target
	tbz w1, #2, weak_target
	ret
	.type local_target, %function
local_target:
	ret
	.global global_target
	.type global_target, %function
global_target:
	ret
	.weak weak_target
	.type weak_target, %function
weak_target:
	ret
	.global hidden_target
	.hidden hidden_target
	.type hidden_target, %function
hidden_target:
	ret
