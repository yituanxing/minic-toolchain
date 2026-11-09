typedef int (*exit_handler_t)(int);
extern int invoke_handlers(int (*exit_handlers[])(int), int);
extern int invoke_typedef_handlers(exit_handler_t exit_handlers[], int);

static int plus_three(int n) { return n + 3; }
static int times_five(int n) { return n * 5; }

int main(void) {
    exit_handler_t handlers[2] = {plus_three, times_five};
    if (invoke_handlers(handlers, 8) != 40) return 1;
    if (invoke_typedef_handlers(handlers, 8) != 11) return 2;
    return 0;
}
