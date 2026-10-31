/* GCC GNU Linux uses strong function designator == NULL BUILD_BUG_ON guards.
 * A missing weak function is different and MUST remain observable at link. */
extern void optional_weak_hook(void) __attribute__((weak));
extern void __compiletime_assert_strong_function_null(void)
    __attribute__((error("strong function designator cannot be NULL")));

static int definite_hook(void) { return 9; }

int strong_designator_guard(void) {
    if ((int (*)(void))definite_hook == (void *)0)
        __compiletime_assert_strong_function_null();
    if ((void *)0 == (int (*)(void))definite_hook)
        __compiletime_assert_strong_function_null();
    return definite_hook();
}

int weak_designator_guard(void) {
    return optional_weak_hook == (void *)0 ? 5 : 13;
}

int main(void) {
    return strong_designator_guard() == 9 && weak_designator_guard() == 5
        ? 0 : 1;
}
