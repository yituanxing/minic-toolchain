struct runtime_record_array_probe {
    int value;
};

int invalid_runtime_record_array_backward_designator(void) {
    struct runtime_record_array_probe items[3] = {
        [1] = {.value = 1},
        [0] = {.value = 2},
    };
    return items[0].value;
}
