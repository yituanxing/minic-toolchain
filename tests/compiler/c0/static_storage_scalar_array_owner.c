enum StaticArraySlot {
    STATIC_ARRAY_ZERO = 0,
    STATIC_ARRAY_ONE = 1,
    STATIC_ARRAY_THREE = 3,
};

static int global_target;
const char *const external_names[] = {
    [STATIC_ARRAY_ONE] = "one",
    [STATIC_ARRAY_THREE] = "three",
};
static int *external_objects[] = {
    [STATIC_ARRAY_ONE] = &global_target,
    [STATIC_ARRAY_THREE] = &global_target,
};

static int probe(void) {
    static const char *const local_names[] = {
        [STATIC_ARRAY_THREE] = "three",
        [STATIC_ARRAY_ZERO] = "zero",
    };
    static const unsigned char public_table[8] = {
        [0 ... 7] = 0,
        [STATIC_ARRAY_THREE] = 1,
    };
    static const char migration_types[4] = {
        [STATIC_ARRAY_ZERO] = 'U',
        [STATIC_ARRAY_THREE] = 'M',
    };
    static const int matrix[2][2] = {
        {1, 2},
        {3, 4},
    };

    return external_names[1][0] == 'o' && external_names[3][0] == 't' &&
                   external_objects[1] == &global_target && external_objects[3] == &global_target &&
                   local_names[0][0] == 'z' && local_names[3][0] == 't' && public_table[2] == 0 &&
                   public_table[3] == 1 && migration_types[0] == 'U' && migration_types[3] == 'M' &&
                   matrix[0][1] == 2 && matrix[1][0] == 3
               ? 0
               : 1;
}

int main(void) {
    return probe();
}
