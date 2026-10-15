static int value;
static int *selected = 1 ? &value : 0;
static int *null_selected = 0 ? &value : 0;
static void *absolute_pointer_poison = (void *)0x300UL + 0xdead000000000000UL;
static void *absolute_pointer_bits = (void *)(0xeB9FUL + 0xdead000000000000UL);
static int static_pointer_function(void) {
    return 0;
}
static void *same_function_conditional = static_pointer_function == (void *)0
                                             ? (void *)&static_pointer_function
                                             : (void *)&static_pointer_function;

int main(void) {
    return selected == &value && null_selected == 0 ? 0 : 1;
}
