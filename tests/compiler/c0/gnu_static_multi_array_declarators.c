/* GNU Linux bq27xxx_battery.c: one static u8 declaration defines many
 * register tables, each with a separate designated initializer. */
static unsigned char first_regs[5] = {
    [0] = 0x10, [3] = 0x33
}, second_regs[5] = {
    [1] = 0x21, [4] = 0x44
}, third_regs[5] = {
    [2] = 0x32
};

static int inferred_first[] = {3, 5}, inferred_second[] = {7, 11, 13};

static int mixed_values[2] = {17, 19}, mixed_scalar = 23,
           *mixed_pointer = mixed_values;

static const char *const labels[2] = {"left", "right"},
                  *const more_labels[2] = {"up", "down"};

typedef unsigned short pair[2];
static pair pair_first = {29, 31}, pair_second = {37, 41};

int main(void) {
    if (first_regs[0] != 0x10 || first_regs[3] != 0x33 ||
        second_regs[1] != 0x21 || second_regs[4] != 0x44 ||
        third_regs[2] != 0x32 || inferred_first[1] != 5 ||
        inferred_second[2] != 13 || mixed_values[0] != 17 ||
        mixed_scalar != 23 || mixed_pointer != mixed_values ||
        labels[0][0] != 'l' || more_labels[1][0] != 'd' ||
        pair_first[1] != 31 || pair_second[0] != 37) {
        return 1;
    }
    return 0;
}
