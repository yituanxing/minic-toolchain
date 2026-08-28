typedef int signed_word_t __attribute__((__mode__(__word__)));
typedef unsigned int unsigned_word_t __attribute__((__mode__(__word__)));

extern double ld_probe_a(double, long double);
extern double ld_probe_b(double, double long);

_Static_assert(sizeof(long double) == 16, "RV64 long double size");
_Static_assert(_Alignof(long double) == 16, "RV64 long double alignment");
_Static_assert(sizeof(signed_word_t) == 8, "RV64 signed word mode size");
_Static_assert(sizeof(unsigned_word_t) == 8, "RV64 unsigned word mode size");

int main(void) {
    signed_word_t s = -1;
    unsigned_word_t u = 1;
    return (s < 0 && u == 1) ? 0 : 1;
}
