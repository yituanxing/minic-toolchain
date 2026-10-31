typedef struct { unsigned reg:24; unsigned bank_idx:4; } Register;

static unsigned read_register(Register x, unsigned value) {
    return x.reg + 13U * x.bank_idx + value;
}

unsigned const_record_compound_arg(unsigned port, unsigned line) {
    unsigned total = 0U;
    for (unsigned i = 0U; i < 2U; ++i) {
        total += read_register(
            (const Register){ .reg = 5U + 0x1000U * (port + line),
                              .bank_idx = line + i },
            17U + i);
    }
    return total;
}

int main(void) { return const_record_compound_arg(2U, 3U) == 41096U ? 0 : 1; }
