void core_m25_discard_pointer(unsigned int *value);
void core_m25_discard_call(unsigned int *value);
unsigned int *core_m25_le32_shape(unsigned int *buf, unsigned int words);

int main(void) {
    unsigned int value = 5U;
    unsigned int words[5] = {0U, 0U, 0U, 0U, 0U};

    core_m25_discard_pointer(&value);
    if (value != 5U) {
        return 1;
    }
    core_m25_discard_call(&value);
    if (value != 8U) {
        return 2;
    }
    if (core_m25_le32_shape(words, 3U) != &words[3]) {
        return 3;
    }
    return 0;
}
