static const char table[4] = {0, 128, 255, 1};

struct Pair {
    char byte;
    int value;
};

char narrow_to_char(int value)
{
    return value;
}

char store_at(char *base, int index, int value)
{
    base[index] = value;
    return base[index];
}

int main(void)
{
    char values[3];
    char local;
    char *pointer;
    struct Pair pair;
    struct Pair *pair_pointer;

    local = 255;
    if (local < 0) {
        return 1;
    }
    if (local != 255) {
        return 2;
    }
    if (table[1] != 128) {
        return 3;
    }
    if (table[2] != 255) {
        return 4;
    }

    values[0] = 0;
    values[1] = 0;
    values[2] = 0;
    pointer = &values[0];
    if (store_at(pointer, 1, 258) != 2) {
        return 5;
    }
    if (*(pointer + 1) != 2) {
        return 6;
    }
    pointer[2] = 255;
    if (values[2] != 255) {
        return 7;
    }

    pair_pointer = &pair;
    pair_pointer->byte = 200;
    pair_pointer->value = 7;
    if (pair_pointer->byte != 200) {
        return 8;
    }
    if (pair_pointer->value != 7) {
        return 9;
    }

    if (narrow_to_char(511) != 255) {
        return 10;
    }
    if ((char)258 != 2) {
        return 11;
    }
    return 0;
}
