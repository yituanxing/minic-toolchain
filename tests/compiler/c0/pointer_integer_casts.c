static int value;

static int target(void) {
    return 7;
}

unsigned long object_bits(void) {
    return (unsigned long)&value;
}

unsigned long function_bits(void) {
    return (unsigned long)target;
}

void *function_as_object(void) {
    return (void *)(unsigned long)target;
}

int main(void) {
    return object_bits() != 0 && function_bits() != 0 && function_as_object() != 0 ? 0 : 1;
}
