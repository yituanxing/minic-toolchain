extern void mini_typed_mmio_exchange(unsigned int *, unsigned int,
                                     unsigned int *, unsigned int *);
extern void gnu_typed_mmio_exchange(unsigned int *, unsigned int,
                                    unsigned int *, unsigned int *);

int main(void) {
    unsigned int mini_word = 7U, gnu_word = 7U;
    unsigned int mini_read = 0U, gnu_read = 0U;
    unsigned int mini_old = 0U, gnu_old = 0U;
    mini_typed_mmio_exchange(&mini_word, 42U, &mini_read, &mini_old);
    gnu_typed_mmio_exchange(&gnu_word, 42U, &gnu_read, &gnu_old);
    if (mini_word != gnu_word || mini_read != gnu_read ||
        mini_old != gnu_old || mini_word != 43U || mini_read != 7U ||
        mini_old != 42U) return 1;
    return 42;
}
