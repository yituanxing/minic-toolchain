/* MiniC half of the LoongArch LP64D cross-compiler aggregate-result ABI.
 * An indirect call with a 17+/24+/40-byte result passes hidden $a0,
 * named arguments in $a1+, and returns the caller's result buffer in $a0. */
struct L17 { unsigned char b[17]; };
struct L24 { unsigned long w[3]; };
struct L40 { unsigned long w[5]; };

extern struct L17 (*make17)(unsigned char);
extern struct L24 (*make24)(unsigned long);
extern struct L40 (*make40)(unsigned long, unsigned long, unsigned long,
                           unsigned long, unsigned long, unsigned long,
                           unsigned long, unsigned long, unsigned long,
                           unsigned long);

struct L17 minic_indirect_17(unsigned char v) {
    return make17(v);
}

struct L24 minic_indirect_24(unsigned long v) {
    struct L24 intermediate = make24(v);
    intermediate.w[2] += 5UL;
    return intermediate;
}

struct L40 minic_indirect_40(unsigned long v) {
    return make40(1UL, 2UL, 3UL, 4UL, 5UL, 6UL,
                  7UL, 8UL, 9UL, v);
}
