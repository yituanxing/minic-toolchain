typedef int (*entry_like_t)(void);

extern entry_like_t start_like[], end_like[];

static entry_like_t *start_address(void) {
    return start_like;
}

static entry_like_t *end_address(void) {
    return end_like;
}

int main(void) {
    return start_address() == end_address();
}
