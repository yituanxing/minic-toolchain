void *dereference_symbol_descriptor(void *ptr) {
    return ptr;
}

void arch_rethook_trampoline(void) {}

unsigned long linux_shaped_direct(void) {
    return (unsigned long)dereference_symbol_descriptor(arch_rethook_trampoline);
}

unsigned long through_function_pointer(void (*callback)(void)) {
    return (unsigned long)dereference_symbol_descriptor(callback);
}

int drive_function_pointer(void) {
    return through_function_pointer(arch_rethook_trampoline) != 0UL;
}
