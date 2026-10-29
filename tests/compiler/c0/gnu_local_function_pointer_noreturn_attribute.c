/* arch/arm64/kernel/hibernate.c declares its local resume trampoline as
 * `void __noreturn (*hibernate_exit)(...)`: function, not object, attribute. */
void never_return(int value) __attribute__((noreturn));
void never_return(int value) {
    for (;;) {
        (void)value;
    }
}

int main(void) {
    void __attribute__((noreturn)) (*hibernate_exit)(int) = never_return;
    return hibernate_exit != never_return;
}
