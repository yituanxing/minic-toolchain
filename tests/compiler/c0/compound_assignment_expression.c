static int storage = 4;

static int *next_slot(void) {
    return &storage;
}

static int update_once(void) {
    int result = (*next_slot() += 3);
    return result + storage;
}

static int *advance_pointer(int *pointer) {
    pointer += 2;
    return pointer;
}

static void *advance_void_pointer(void *pointer) {
    pointer += 3;
    return pointer;
}

static unsigned long long divide_unsigned(unsigned long long value) {
    value /= 10;
    return value;
}

static long long divide_signed(long long value) {
    value /= 10;
    return value;
}

int main(void) {
    int values[4];
    return update_once() == 14 && advance_pointer(values) == values + 2 &&
                   advance_void_pointer(values) == (void *)((char *)values + 3) &&
                   divide_unsigned(100ULL) == 10ULL && divide_signed(-100LL) == -10LL
               ? 0
               : 1;
}
