/* Regression for mutable locals and specialized integer parameters.
 * Bounded failure escapes prevent optimizer errors from hanging a test. */
static unsigned local_update_sum(const unsigned char *p, unsigned length)
{
    unsigned result=0;
    unsigned loops=0;
    while (length > 12U) {
        result+=(unsigned)p[0]+(unsigned)p[4]+(unsigned)p[8];
        p+=12;
        length-=12U;
        if (++loops > 3U) return 0xffffffffU;
    }
    return result+length+loops;
}

static int local_update_count(int enabled)
{
    int cnt=0;
    if (enabled) cnt++;
    if (cnt == 0 && enabled) return -1;
    if (enabled) cnt+=2;
    if (cnt != (enabled ? 3 : 0)) return -2;
    cnt--;
    if (cnt != (enabled ? 2 : -1)) return -3;
    return cnt;
}

int local_update_fact_barrier_entry(void)
{
    static const unsigned char data[16] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
    };
    if (local_update_sum(data,16U) != 1U+5U+9U+4U+1U) return 1;
    if (local_update_sum(data,4U) != 4U) return 2;
    if (local_update_count(1) != 2) return 3;
    if (local_update_count(0) != -1) return 4;
    return 0;
}
