extern double ld_probe_a(double, long double);
extern double ld_probe_b(double, double long);

_Static_assert(sizeof(long double) == 16, "RV64 long double size");
_Static_assert(_Alignof(long double) == 16, "RV64 long double alignment");

int main(void) {
    return 0;
}
