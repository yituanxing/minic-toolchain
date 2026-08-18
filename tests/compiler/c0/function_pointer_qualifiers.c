typedef int callback_t(int value);

struct Ops {
    int (*const filter)(int value);
    int (*volatile hook)(int value);
};

typedef int (*const const_callback_t)(int value);

int call_filter(struct Ops *ops, int value) {
    return ops->filter(value);
}

int call_hook(struct Ops *ops, int value) {
    return ops->hook(value);
}

int call_typedef(const_callback_t callback, int value) {
    return callback(value);
}

/* Array suffixes belong to the function-pointer declarator, not the function type. */
extern int (*external_handlers[])(int value);
static int (*static_handlers[1])(int value) = {0};

int function_pointer_array_shape(void) {
    return static_handlers[0] == 0;
}
