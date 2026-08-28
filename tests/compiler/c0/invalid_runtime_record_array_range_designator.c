struct runtime_record_array_probe {
    int value;
};

int invalid_runtime_record_array_range_designator(void) {
    struct runtime_record_array_probe items[3] = {
        [0 ... 1] = {.value = 1},
    };
    return items[0].value;
}
