/* A byte addend into a multi-dimensional array must not excuse an unrelated
 * pointer target type. No explicit cast is present here. */
static const unsigned int regs[3][5] = {{1}, {2}, {3}};
struct InvalidPll { const unsigned long *regs; };
static struct InvalidPll invalid = { .regs = regs[1] };
