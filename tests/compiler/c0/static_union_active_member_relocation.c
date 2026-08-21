static int target = 7;

union payload {
    long canonical;
    struct {
        int first;
        int second;
    } pair;
};

struct holder {
    union payload payload;
    int *pointer;
};

static struct holder state = {
    .payload.pair = { .first = 1, .second = 2 },
    .pointer = &target,
};

int main(void) {
    return (state.payload.pair.first == 1 && state.payload.pair.second == 2 &&
            state.pointer == &target && *state.pointer == 7)
               ? 0
               : 1;
}
