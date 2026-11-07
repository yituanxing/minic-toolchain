extern void minic_void_effect(void);

/* GNU result type is void: it cannot initialize an int or be returned
 * from a nonvoid function. */
int minic_invalid_mixed_void_result(int cond) {
    return cond ? minic_void_effect() : 1;
}
