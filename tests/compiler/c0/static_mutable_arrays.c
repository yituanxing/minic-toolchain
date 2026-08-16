static char early_cmdline[2048];
static unsigned long riscv_isa[64];
static int aia_irq2bitpos[] = {0, -1, 0, -1, 2};
static const unsigned int fixed_values[4] = {1, 2, 3, 0};

int main(void) {
    early_cmdline[0] = 'x';
    riscv_isa[1] = 3;
    return aia_irq2bitpos[1] + (int)fixed_values[2] + (int)riscv_isa[1] +
           (int)early_cmdline[0];
}
