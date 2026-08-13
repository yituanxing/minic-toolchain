union Payload {
    long wide;
    int words[2];
};

struct SemaphoreLike {
    unsigned int count;
    struct { void *next; void *prev; } wait;
};

static void initialize_through_pointer(struct SemaphoreLike *sem, int value) {
    *sem = (struct SemaphoreLike){ .count = (unsigned int)value, .wait = { &sem->wait, &sem->wait } };
}

int main(void) {
    union Payload left;
    union Payload right;

    right.wide = 123;
    (left = right, (void)0);
    return left.wide == 123 ? 0 : 1;
}
