/* Linux EFI uses genuinely undefined weak objects and functions: a missing
 * provider is legal and must link as address zero.  Global initializers and
 * live Core both need to retain the weak ELF binding. */
extern int missing_object __attribute__((weak));
__attribute__((weak)) __attribute__((visibility("hidden")))
extern int hidden_missing_object;
extern int provided_object __attribute__((weak));
extern int missing_function(void) __attribute__((weak));
__attribute__((weak)) __attribute__((visibility("hidden")))
extern int hidden_missing_function(void);
extern int provided_function(void) __attribute__((weak));
extern int unused_weak_object __attribute__((weak));
extern int unused_weak_function(void) __attribute__((weak));

static int *object_addresses[] = {
    &missing_object, &hidden_missing_object, &provided_object
};
static int (*function_addresses[])(void) = {
    missing_function, hidden_missing_function, provided_function
};

/* A global initializer can carry a weak absolute relocation; a live Core
 * address expression additionally tests the target's code relocation. On
 * AArch64 an undefined weak may be zero, even under kernel -fno-pic. */
static int weak_runtime_addresses(void) {
    return &missing_object == 0 && &hidden_missing_object == 0 &&
           missing_function == 0 && hidden_missing_function == 0 &&
           &provided_object != 0 && provided_function != 0;
}

int main(void) {
    if (!weak_runtime_addresses()) return 3;
    if (object_addresses[0] != 0 || object_addresses[1] != 0 ||
        object_addresses[2] == 0 || *object_addresses[2] != 17)
        return 1;
    if (function_addresses[0] != 0 || function_addresses[1] != 0 ||
        function_addresses[2] == 0 || function_addresses[2]() != 23)
        return 2;
    return 0;
}
