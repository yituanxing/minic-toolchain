/* AAPCS64 binary128 is passed and returned in Q registers; Core marks
 * intrinsic TF operations by type, not by their runtime fallback names.
 * Exercise every sealed Core TF operation against the MachineIR selector. */
long double tf_add(long double x, long double y) { return x + y; }
long double tf_sub(long double x, long double y) { return x - y; }
long double tf_mul(long double x, long double y) { return x * y; }
long double tf_div(long double x, long double y) { return x / y; }
long double tf_neg(long double x) { return -x; }

int tf_eq(long double x, long double y) { return x == y; }
int tf_lt(long double x, long double y) { return x < y; }
int tf_le(long double x, long double y) { return x <= y; }

long double tf_from_f32(float x) { return (long double)x; }
long double tf_from_f64(double x) { return (long double)x; }
float tf_to_f32(long double x) { return (float)x; }
double tf_to_f64(long double x) { return (double)x; }

long double tf_from_s32(int x) { return (long double)x; }
long double tf_from_u32(unsigned int x) { return (long double)x; }
long double tf_from_s64(long long x) { return (long double)x; }
long double tf_from_u64(unsigned long long x) { return (long double)x; }
int tf_to_s32(long double x) { return (int)x; }
unsigned int tf_to_u32(long double x) { return (unsigned int)x; }
long long tf_to_s64(long double x) { return (long long)x; }
unsigned long long tf_to_u64(long double x) { return (unsigned long long)x; }
