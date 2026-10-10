/* Minimum earlycon-style scan: a mutable _Bool gates a second pass
 * through a for-loop reached through a backward goto. No Linux dependency.
 * Regression witness: pristine MiniC PASS, production 39-patch runtime
 * profile FAIL. The first bad buildable prefix is #32, an inline-asm
 * symbolic specialization wrapper invokes seven follow-on patches;
 * the last, unreachable-reentry, expands to another twenty CFG patches.
 * The focused regression now isolates them incrementally on ONE runner. */
typedef _Bool bool;
static int scan_count;
static int table[3] = {1, 3, 5};

int earlycon_probe_not_found(int wanted)
{
    bool second_pass = 1;
    int i;
again:
    for (i = 0; i < 3; ++i) {
        ++scan_count;
        /* Bound buggy loops to give CI a deterministic error rather than hang. */
        if (scan_count > 16)
            return 99;
        if (table[i] == wanted && !second_pass)
            return 100 + scan_count;
    }
    if (second_pass) {
        second_pass = 0;
        goto again;
    }
    return scan_count;
}

int earlycon_probe_entry(void)
{
    int result;
    scan_count = 0;
    result = earlycon_probe_not_found(7);
    if (result != 6 || scan_count != 6)
        return 1;
    scan_count = 0;
    result = earlycon_probe_not_found(3);
    if (result != 105 || scan_count != 5)
        return 2;
    return 0;
}
