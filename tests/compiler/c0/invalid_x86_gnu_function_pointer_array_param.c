typedef long (*wrong_handler_t)(long);
extern int invoke_handlers(int (*exit_handlers[])(int), int);
int bad_call(wrong_handler_t handlers[]) {
    /* Different return and argument types are not pointer-compatible. */
    return invoke_handlers(handlers, 8);
}
