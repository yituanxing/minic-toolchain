void __attribute__((__section__(".text.preptr"))) __attribute__((__noinline__)) *
    map_before_pointer(int value);

void *map_before_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

void *__attribute__((noinline)) map_after_pointer(int value);

void *map_after_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

char *__attribute__((__unused__)) *interleaved_return_pointer_attribute(void) {
    return (char **)0;
}

extern char __attribute__((__section__(".data.preptr"))) * extern_slot_before_pointer;

int use_deferred_declarator_attributes(void) {
    return map_before_pointer(1) == (void *)0 && map_after_pointer(2) == (void *)0;
}

void __attribute__((__noinline__)) __attribute__((__noclone__)) kfree_skb_reason_shape(int reason);

void kfree_skb_reason_shape(int reason) {
    (void)reason;
}

void __attribute__((noclone)) * noclone_after_return_pointer(int value);

void *noclone_after_return_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

static int __attribute__((__used__)) used_object_shape = 7;

void __attribute__((used)) used_function_shape(void);

void used_function_shape(void) {
    (void)used_object_shape;
}

void (*__attribute__((__section__(".init.fp-object"))) late_time_init_shape)(void);

int parameter_attribute_before_pointer(void __attribute__((__unused__)) *data) {
    return data != (void *)0;
}
