struct task_struct;

register struct task_struct *riscv_current_is_tp __asm__("tp");
register unsigned long current_stack_pointer __asm__("sp");

struct task_struct *read_current_like(void) {
    return riscv_current_is_tp;
}

unsigned long read_stack_pointer_like(void) {
    return current_stack_pointer;
}
