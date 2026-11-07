int external_value = 5;

int main(void) {
    __attribute__((__unused__)) unsigned int *before_stage;
    __attribute__((__unused__)) unsigned int *start_stage = (unsigned int *)0;
    __attribute__((__unused__)) extern int external_value;
    __attribute__((__unused__)) unsigned int result = (unsigned int)external_value + 3U;

    (void)start_stage;
    return result == 8U ? 0 : 1;
}
