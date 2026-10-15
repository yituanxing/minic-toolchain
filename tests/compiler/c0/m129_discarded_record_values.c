typedef struct {
    unsigned long bits;
} record_word_t;

static record_word_t make_word(unsigned long value) {
    return (record_word_t){value + 1UL};
}

int main(void) {
    record_word_t left = {1UL};
    record_word_t right = {2UL};
    left = 1 ? right : make_word(3UL);
    make_word(4UL);
    return left.bits == 2UL ? 0 : 1;
}
