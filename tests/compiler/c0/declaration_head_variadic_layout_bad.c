typedef __attribute__((section(".bad"))) int (*bad_function_t)(int, ...);

int main(void) {
    return 0;
}
