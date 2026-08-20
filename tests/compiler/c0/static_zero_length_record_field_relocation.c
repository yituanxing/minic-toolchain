static int target = 7;

struct payload {
    long a;
    long b;
    long c;
    long d;
    long e;
    long f;
    long g;
};

struct holder {
    int before;
    struct payload zero[0];
    int *pointer;
    int after;
    int *tail[];
};

static struct holder state = {
    .before = 3,
    .pointer = &target,
    .after = 5,
};

int main(void) {
    return (state.before == 3 && state.pointer == &target && *state.pointer == 7 &&
            state.after == 5)
               ? 0
               : 1;
}
