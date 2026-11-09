/* Original Linux 6.6.143 arch/loongarch/kernel/relocate.c model: GNU
 * global hard-register variable is an assignable architectural TP alias. */
struct thread_info { unsigned long sentinel; };
register struct thread_info *__current_thread_info asm("$tp");

struct thread_info *loong_relocate_thread_info(unsigned long random_offset) {
    struct thread_info *before = __current_thread_info;
    __current_thread_info =
        (void *)((long)__current_thread_info + random_offset);
    return before;
}
