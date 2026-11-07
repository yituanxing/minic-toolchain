/* The LP64 source-object stack pool must use the existing RV Core interval
 * proof.  Temporary address-backed scalars with disjoint lifetimes may share
 * storage; pointers that escape and locals read across CFG joins may not. */

#define LOCAL_STEP(k) \
    volatile int local_##k = (seed ^ (k)) + 3; \
    sum += local_##k;

volatile int *lp64_slot_first;
volatile int *lp64_slot_second;

static void retain_both(volatile int *first, volatile int *second) {
    lp64_slot_first = first;
    lp64_slot_second = second;
}

int lp64_disjoint_local_slots(int seed) {
    int sum = 0;
    LOCAL_STEP(1); LOCAL_STEP(2); LOCAL_STEP(3); LOCAL_STEP(4);
    LOCAL_STEP(5); LOCAL_STEP(6); LOCAL_STEP(7); LOCAL_STEP(8);
    LOCAL_STEP(9); LOCAL_STEP(10); LOCAL_STEP(11); LOCAL_STEP(12);
    LOCAL_STEP(13); LOCAL_STEP(14); LOCAL_STEP(15); LOCAL_STEP(16);
    LOCAL_STEP(17); LOCAL_STEP(18); LOCAL_STEP(19); LOCAL_STEP(20);
    LOCAL_STEP(21); LOCAL_STEP(22); LOCAL_STEP(23); LOCAL_STEP(24);
    LOCAL_STEP(25); LOCAL_STEP(26); LOCAL_STEP(27); LOCAL_STEP(28);
    LOCAL_STEP(29); LOCAL_STEP(30); LOCAL_STEP(31); LOCAL_STEP(32);
    LOCAL_STEP(33); LOCAL_STEP(34); LOCAL_STEP(35); LOCAL_STEP(36);
    LOCAL_STEP(37); LOCAL_STEP(38); LOCAL_STEP(39); LOCAL_STEP(40);
    LOCAL_STEP(41); LOCAL_STEP(42); LOCAL_STEP(43); LOCAL_STEP(44);
    LOCAL_STEP(45); LOCAL_STEP(46); LOCAL_STEP(47); LOCAL_STEP(48);
    LOCAL_STEP(49); LOCAL_STEP(50); LOCAL_STEP(51); LOCAL_STEP(52);
    LOCAL_STEP(53); LOCAL_STEP(54); LOCAL_STEP(55); LOCAL_STEP(56);
    LOCAL_STEP(57); LOCAL_STEP(58); LOCAL_STEP(59); LOCAL_STEP(60);
    LOCAL_STEP(61); LOCAL_STEP(62); LOCAL_STEP(63); LOCAL_STEP(64);
    return sum;
}

int lp64_escape_preserves_independent_slots(int seed) {
    int first = seed + 11;
    int second = seed + 19;
    retain_both(&first, &second);
    if (lp64_slot_first == lp64_slot_second) return -5000;
    first += seed;
    second += seed * 2;
    return *lp64_slot_first * 3 + *lp64_slot_second * 5;
}

int lp64_crossblock_live_value(int seed) {
    int preserved = seed + 7;
    int result = 0;
    if ((seed & 1) != 0) goto JOIN;
    result += seed + 2;
JOIN:
    result += preserved;
    {
        int later = seed * 3;
        result += later;
    }
    return result;
}

int main(void) {
    const int seed = 13;
    const int expected_disjoint = lp64_disjoint_local_slots(seed);
    const int expected_escape = lp64_escape_preserves_independent_slots(seed);
    const int expected_join = lp64_crossblock_live_value(seed);
    return expected_disjoint == 2272 && expected_escape == 401 &&
           expected_join == 59 ? 42 : 1;
}
