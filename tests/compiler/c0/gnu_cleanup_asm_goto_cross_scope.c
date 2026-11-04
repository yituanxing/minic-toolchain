static void cleanup_touch(int *guard) { (void)guard; }

void asm_goto_leaves_cleanup_scope(void) {
    {
        int guard __attribute__((cleanup(cleanup_touch))) = 1;
        asm goto("jmp %l[outside] # %0" : : "i"(7) : : outside);
        (void)guard;
    }
outside:
    return;
}
