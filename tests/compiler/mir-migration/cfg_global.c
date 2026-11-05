/* CFG/label identity, relocation-bearing globals and data are all visible
 * at GNU -S without a target cross-assembler. */
long mir_table[4] = {3, 5, 7, 11};
long *mir_global_ptr = mir_table;
volatile int mir_route;

__attribute__((noinline))
long mir_cfg(int selector, long x) {
    long result;
    if (selector < 0) goto negative;
    switch (selector) {
    case 0: result = mir_table[0]; break;
    case 1: result = mir_table[1] + x; break;
    case 3: result = mir_table[3] - x; break;
    default: result = mir_global_ptr[2] ^ x; break;
    }
    goto finish;
negative:
    result = -x;
finish:
    mir_route = selector;
    return result;
}

int main(void) {
    return (int)(mir_cfg(1, 4) + mir_cfg(-1, 9));
}
