/* Cross-ABI aggregate passing, nested records, hidden structure returns,
 * object copies, and pointer aliases.  No host-system headers required. */
struct MirInner { long value; unsigned short tag; };
struct MirRecord {
    struct MirInner inner[3];
    long tail[4];
    unsigned char status;
};

volatile long mir_aggregate_sink;

__attribute__((noinline))
struct MirRecord mir_make_record(long a, long b, long c) {
    struct MirRecord result = {0};
    result.inner[0].value = a + 3;
    result.inner[1].value = b * 7;
    result.inner[2].value = c - a;
    result.inner[1].tag = (unsigned short)(a ^ b);
    result.tail[0] = a;
    result.tail[1] = b;
    result.tail[2] = c;
    result.tail[3] = result.inner[1].value + result.inner[0].value;
    result.status = (unsigned char)(result.tail[3] & 255L);
    return result;
}

__attribute__((noinline))
long mir_use_record(struct MirRecord by_value, struct MirInner small,
                    long extra) {
    long sum = by_value.tail[0] + by_value.tail[3] + extra;
    sum += by_value.inner[1].value + small.value + small.tag;
    mir_aggregate_sink = sum;
    return sum;
}

int main(void) {
    struct MirRecord local = mir_make_record(5, 11, 17);
    struct MirInner tiny = local.inner[1];
    return (int)(mir_use_record(local, tiny, 29) & 255L);
}
