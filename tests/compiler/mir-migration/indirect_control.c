/* Computed goto, indirect calls, branch-sensitive SSA values and switch.
 * Address-taken entry labels must not jump back through a prologue. */
typedef long (*MirCall)(long, long);

__attribute__((noinline))
long mir_indirect_target(long a, long b) { return (a << 1) - b; }

__attribute__((noinline))
long mir_dispatch(int code, long value, MirCall call) {
    static void *const table[] = {&&s0, &&s1, &&s2, &&s3};
    long result = value;
    if ((unsigned)code >= 4U) goto done;
    goto *table[code];
s0:
    result += 19;
    goto done;
s1:
    result = call(result, 5);
    goto done;
s2:
    result ^= 0x55AAL;
    goto done;
s3:
    switch ((int)(result & 3L)) {
    case 0: result += 1; break;
    case 1: result *= 3; break;
    case 2: result >>= 2; break;
    default: result -= 9; break;
    }
done:
    return result;
}

int main(void) {
    return (int)(mir_dispatch(1, 41, mir_indirect_target) & 255L);
}
