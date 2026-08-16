enum slot_index { SLOT_ONE = 1, SLOT_THREE = 3 };

struct pair {
    int left;
    int right;
};

struct holder {
    struct pair limits[4];
};

typedef struct pair pair_t;

static const struct pair sparse_records[] = {
    [SLOT_ONE] = {.left = 11, .right = 12},
    [SLOT_THREE] = (struct pair){.left = 31, .right = 32},
};

static const struct holder nested_records = {
    .limits =
        {
            [1] = {.left = 21, .right = 22},
            [3] = {.left = 41, .right = 42},
        },
};

static const pair_t fixed_records[4] = {
    [2] = ((pair_t){.left = 51, .right = 52}),
};

int main(void) {
    return sparse_records[1].left == 11 && sparse_records[3].right == 32 &&
                   nested_records.limits[1].right == 22 && nested_records.limits[3].left == 41 &&
                   fixed_records[2].right == 52
               ? 0
               : 1;
}
