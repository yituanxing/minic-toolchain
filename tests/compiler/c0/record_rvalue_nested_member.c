/* A GNU/ISO C aggregate rvalue has one temporary object: nested member
 * projections walk that one materialization without re-running the source
 * call. Linux arm64 mmu.c has ((pud_t){...}).p4d.pgd.pgd chains. */
struct Leaf { unsigned long value; };
struct Mid { struct Leaf leaf; unsigned long extra; };
struct Outer { struct Mid mid; unsigned long tail; };

static int evaluations;

static struct Outer produce(unsigned long value) {
    struct Outer result;
    evaluations += 1;
    result.mid.leaf.value = value + 11UL;
    result.mid.extra = value + 21UL;
    result.tail = value + 31UL;
    return result;
}

static struct Mid pass_mid(unsigned long value) {
    return produce(value).mid;
}

static struct Leaf pass_leaf(unsigned long value) {
    return produce(value).mid.leaf;
}

int main(void) {
    unsigned long one = produce(3UL).mid.leaf.value;
    struct Mid two = produce(4UL).mid;
    struct Leaf three = produce(5UL).mid.leaf;
    unsigned long four = (1 ? produce(6UL) : produce(7UL)).mid.leaf.value;
    struct Mid five = pass_mid(8UL);
    struct Leaf six = pass_leaf(9UL);

    if (evaluations != 6 || one != 14UL || two.leaf.value != 15UL ||
        two.extra != 25UL || three.value != 16UL || four != 17UL ||
        five.leaf.value != 19UL || five.extra != 29UL || six.value != 20UL)
        return 1;
    return 0;
}
