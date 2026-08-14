typedef unsigned int poll_mask_t;
typedef poll_mask_t (*poll_fn_t)(void *file, void *table);

_Bool return_function_pointer(poll_fn_t poll) {
    return poll;
}

_Bool return_object_pointer(void *pointer) {
    return pointer;
}

int assign_function_pointer(poll_fn_t poll) {
    _Bool available;
    available = poll;
    return available;
}

int assign_object_pointer(void *pointer) {
    _Bool available;
    available = pointer;
    return available;
}

static int accept_bool(_Bool value) {
    return value;
}

int pass_function_pointer(poll_fn_t poll) {
    return accept_bool(poll);
}
