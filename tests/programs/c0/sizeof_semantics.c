struct Pair {
    int value;
    char tag;
};

static int bump(int *value)
{
    *value = *value + 1;
    return *value;
}

int main(void)
{
    int values[3];
    int side_effect;
    struct Pair pair;

    side_effect = 0;

    if (sizeof("") != 1) {
        return 1;
    }
    if (sizeof("abc") != 4) {
        return 2;
    }
    if (sizeof(char) != 1 || sizeof(int) != 4 || sizeof(long) != 8) {
        return 3;
    }
    if (sizeof(int *) != 8 || sizeof(double) != 8 || sizeof(float) != 4) {
        return 4;
    }
    if (sizeof(values) != 12) {
        return 5;
    }
    if (sizeof(pair) != sizeof(struct Pair) || sizeof(struct Pair) != 8) {
        return 6;
    }
    if (sizeof(bump(&side_effect)) != 4) {
        return 7;
    }
    if (side_effect != 0) {
        return 8;
    }
    return 0;
}
