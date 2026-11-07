/* MC-009: inferred static array extent probes a CALL owning >8 arguments.
 * The CALL is unevaluated inside sizeof, but its Program-owned argument tail
 * must be released when the speculative designator parse rolls back. */
extern int twelves(int, int, int, int, int, int,
                   int, int, int, int, int, int);

static const unsigned char lookup[] = {
    [sizeof(twelves(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)) - 1] = 7
};

int main(void) {
    return lookup[sizeof(int) - 1U];
}
