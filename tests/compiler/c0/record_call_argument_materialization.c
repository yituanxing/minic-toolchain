typedef struct {
    unsigned long bits;
} record_word_t;

static record_word_t make_word(unsigned long value) {
    return (record_word_t){value + 1UL};
}

void consume_word(record_word_t value);

int main(void) {
    consume_word(0 ? (record_word_t){3UL} : make_word(10UL));
    return 0;
}
