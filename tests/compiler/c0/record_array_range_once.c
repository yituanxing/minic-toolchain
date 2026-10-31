/* Linux i915 rc6 initializes register descriptors with [0 ... N-1].
 * The GNU range initializer must evaluate one record expression only once. */
struct Reg { unsigned int address; };

static int evaluations;

static struct Reg make_reg(unsigned int value) {
    struct Reg result;
    ++evaluations;
    result.address = value;
    return result;
}

int main(void) {
    struct Reg regs[5] = {
        [1 ... 3] = make_reg(99U),
    };
    struct Reg constants[4] = {
        [0 ... 3] = ((const struct Reg){ .address = 21U }),
    };
    if (evaluations != 1 || regs[0].address != 0U ||
        regs[1].address != 99U || regs[2].address != 99U ||
        regs[3].address != 99U || regs[4].address != 0U ||
        constants[0].address != 21U || constants[3].address != 21U)
        return 1;
    return 0;
}
