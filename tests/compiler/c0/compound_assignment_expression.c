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

int main(void) {
    int values[4];
    return update_once() == 14 && advance_pointer(values) == values + 2 ? 0 : 1;
}
