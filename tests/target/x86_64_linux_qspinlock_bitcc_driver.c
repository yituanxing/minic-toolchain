extern unsigned int minic_linux_qspinlock_bitcc(unsigned int *);

int main(void) {
    unsigned int word = 0U;
    if (minic_linux_qspinlock_bitcc(&word) != 0U || word != 256U)
        return 1;
    if (minic_linux_qspinlock_bitcc(&word) != 256U || word != 256U)
        return 2;
    word = 0xfeed00ffU;
    if (minic_linux_qspinlock_bitcc(&word) != 0U || word != 0xfeed01ffU)
        return 3;
    return 0;
}
