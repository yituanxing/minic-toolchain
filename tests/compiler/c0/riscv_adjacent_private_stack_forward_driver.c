/* Freestanding test driver: GCC emits only the harness, MiniC emits cases. */
extern long mini_stack_forward_chain(long, long, long);
extern long mini_stack_forward_call(long, long, long (*)(long));
extern long mini_stack_forward_asm(long, long);
extern long mini_stack_forward_branch(long, long, int);
extern long mini_stack_forward_large(long, long);

static long times_three(long value) { return value * 3; }

int mini_stack_forward_test_main(void) {
    if (mini_stack_forward_chain(-17, 9, 4) != -12) return 11;
    if (mini_stack_forward_chain(100, -7, 20) != 206) return 12;
    if (mini_stack_forward_call(-17, 9, times_three) != -32) return 13;
    if (mini_stack_forward_call(100, -7, times_three) != 372) return 14;
    if (mini_stack_forward_asm(-17, 9) != 1) return 15;
    if (mini_stack_forward_branch(-17, 9, 1) != 1) return 16;
    if (mini_stack_forward_branch(-17, 9, 0) != -25) return 17;
    if (mini_stack_forward_large(-17, 9) != -16) return 18;
    if (mini_stack_forward_large(100, -7) != 186) return 19;
    return 0;
}
