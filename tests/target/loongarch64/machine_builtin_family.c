/* One complete source-only production admission witness for the 25 pinned
 * LoongArch machine builtin signatures (22 distinct native ISA spellings).
 * Privileged CSR/IOCSR instructions are compiled but never executed at PLV3.
 * All calls must become typed Machine opcodes; none may become an ELF BL. */

unsigned int loong_builtin_cpucfg(unsigned int input) {
    return __builtin_loongarch_cpucfg(input);
}

void loong_builtin_assertions(long a, long b) {
    __builtin_loongarch_asrtle_d(a, b);
    __builtin_loongarch_asrtgt_d(a, b);
}

int loong_builtin_crc_w_b_w(char a, int b) {
    return __builtin_loongarch_crc_w_b_w(a, b);
}
int loong_builtin_crc_w_h_w(short a, int b) {
    return __builtin_loongarch_crc_w_h_w(a, b);
}
int loong_builtin_crc_w_w_w(int a, int b) {
    return __builtin_loongarch_crc_w_w_w(a, b);
}
int loong_builtin_crc_w_d_w(long a, int b) {
    return __builtin_loongarch_crc_w_d_w(a, b);
}
int loong_builtin_crcc_w_b_w(char a, int b) {
    return __builtin_loongarch_crcc_w_b_w(a, b);
}
int loong_builtin_crcc_w_h_w(short a, int b) {
    return __builtin_loongarch_crcc_w_h_w(a, b);
}
int loong_builtin_crcc_w_w_w(int a, int b) {
    return __builtin_loongarch_crcc_w_w_w(a, b);
}
int loong_builtin_crcc_w_d_w(long a, int b) {
    return __builtin_loongarch_crcc_w_d_w(a, b);
}

unsigned char loong_builtin_iocsrrd_b(unsigned int index) {
    return __builtin_loongarch_iocsrrd_b(index);
}
unsigned short loong_builtin_iocsrrd_h(unsigned int index) {
    return __builtin_loongarch_iocsrrd_h(index);
}
unsigned int loong_builtin_iocsrrd_w(unsigned int index) {
    return __builtin_loongarch_iocsrrd_w(index);
}
unsigned long loong_builtin_iocsrrd_d(unsigned int index) {
    return __builtin_loongarch_iocsrrd_d(index);
}

void loong_builtin_iocsrwr_b(unsigned char value, unsigned int index) {
    __builtin_loongarch_iocsrwr_b(value, index);
}
void loong_builtin_iocsrwr_h(unsigned short value, unsigned int index) {
    __builtin_loongarch_iocsrwr_h(value, index);
}
void loong_builtin_iocsrwr_w(unsigned int value, unsigned int index) {
    __builtin_loongarch_iocsrwr_w(value, index);
}
void loong_builtin_iocsrwr_d(unsigned long value, unsigned int index) {
    __builtin_loongarch_iocsrwr_d(value, index);
}

unsigned int loong_builtin_csrrd_w(void) {
    return __builtin_loongarch_csrrd_w(32U);
}
unsigned long loong_builtin_csrrd_d(void) {
    return __builtin_loongarch_csrrd_d(16U);
}
unsigned int loong_builtin_csrwr_w(unsigned int value) {
    return __builtin_loongarch_csrwr_w(value, 32U);
}
unsigned long loong_builtin_csrwr_d(unsigned long value) {
    return __builtin_loongarch_csrwr_d(value, 16U);
}
unsigned int loong_builtin_csrxchg_w(unsigned int value, unsigned int mask) {
    return __builtin_loongarch_csrxchg_w(value, mask, 32U);
}
unsigned long loong_builtin_csrxchg_d(unsigned long value,
    unsigned long mask) {
    return __builtin_loongarch_csrxchg_d(value, mask, 16U);
}
