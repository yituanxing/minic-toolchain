static const unsigned long global_words[2] = {1, 2};

static unsigned long pass_unsigned(unsigned long value)
{
    return value;
}

static long pass_signed(long value)
{
    return value;
}

int main(void)
{
    unsigned long one = global_words[0];
    unsigned long high = one << 63;
    unsigned long wide = one << 40;
    unsigned long value = pass_unsigned(high + wide + 37);
    unsigned long product = (wide + 3) * 5;
    long negative = -((long)one << 40);

    if (value < high) {
        return 1;
    }
    if ((value >> 63) != 1) {
        return 2;
    }
    if ((value - high) != wide + 37) {
        return 3;
    }
    if (product / 5 != wide + 3) {
        return 4;
    }
    if (product % 5 != 0) {
        return 5;
    }
    if ((high ^ high) != 0) {
        return 6;
    }
    if (pass_signed(negative) >= 0) {
        return 7;
    }
    if ((negative >> 39) != -2) {
        return 8;
    }
    if ((unsigned long)negative == 0) {
        return 9;
    }
    if (global_words[1] != 2) {
        return 10;
    }
    return 0;
}
