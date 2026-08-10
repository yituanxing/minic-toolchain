static int side_effect_count;

static void *side_effect_pointer(void) {
    side_effect_count += 1;
    return (void *)0;
}

static unsigned long unknown_max(const void *pointer) {
    return __builtin_object_size(pointer, 0);
}

static unsigned long unknown_subobject(const void *pointer) {
    return __builtin_object_size(pointer, 1);
}

static unsigned long unknown_min(const void *pointer) {
    return __builtin_object_size(pointer, 2);
}

static unsigned long known_array(void) {
    unsigned char bytes[9];

    return __builtin_object_size(bytes, 0);
}

int main(void) {
    const void *pointer = (void *)0;

    if (unknown_max(pointer) != ~0UL)
        return 1;
    if (unknown_subobject(pointer) != ~0UL)
        return 2;
    if (unknown_min(pointer) != 0UL)
        return 3;
    if (known_array() != 9UL)
        return 4;
    if (__builtin_object_size(side_effect_pointer(), 0) != ~0UL)
        return 5;
    return side_effect_count == 0 ? 0 : 6;
}
