/* Stage 1: a real process exit and integer control flow, without stdio. */
volatile unsigned ladder_seed = 7U;

int main(void) {
    unsigned total = 0U;
    for (unsigned i = 1U; i <= 100U; ++i)
        total += i * ladder_seed;
    return total == 35350U ? 0 : 17;
}
