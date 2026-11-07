/* Three deliberate non-proofs for global record initializer folding.
 * A change to the object, a volatile read or an unbounded index must not
 * transform a reachable GNU diagnostic call into a constant. */
struct Record {
    unsigned char mode;
    long value;
};

extern void minic_global_record_not_constant(void)
    __attribute__((error("global record field is not immutable and fixed")));

#if defined(TEST_MUTABLE)
static struct Record records[2] = {{1, 17}, {2, 23}};
void minic_global_record_write(long value)
{
    records[0].value = value;
}
#elif defined(TEST_VOLATILE)
static const volatile struct Record records[2] = {{1, 17}, {2, 23}};
#else
static const struct Record records[2] = {{1, 17}, {2, 23}};
#endif

#if defined(TEST_UNKNOWN_INDEX)
int minic_global_record_read(int index)
{
    if (records[index].value != 17)
        minic_global_record_not_constant();
    return 0;
}
#elif defined(TEST_OUT_OF_RANGE)
/* Deliberate undefined C access: only tests MiniC's strict failure to claim
 * an initialized slot, never compares a runtime result with GNU's UB. */
int minic_global_record_read(void)
{
    if (records[2].value != 17)
        minic_global_record_not_constant();
    return 0;
}
#else
int minic_global_record_read(void)
{
    if (records[0].value != 17)
        minic_global_record_not_constant();
    return 0;
}
#endif
