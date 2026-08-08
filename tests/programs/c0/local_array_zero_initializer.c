int main(void) {
    unsigned char values[5] = {0};

    values[2] = 7;
    if (values[0] != 0) {
        return 1;
    }
    if (values[1] != 0) {
        return 2;
    }
    if (values[2] != 7) {
        return 3;
    }
    if (values[3] != 0) {
        return 4;
    }
    if (values[4] != 0) {
        return 5;
    }
    return 0;
}
