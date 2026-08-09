typedef unsigned long size_t;

int main(void) {
    char etalon[1024 * 1024];
    int sum = 0;

    for (size_t i = 0; i < sizeof(etalon); i++) {
        if (i == 4) {
            break;
        }
        sum += (int)i;
    }

    for (int i = 0; i < 3; i++) {
        sum += i;
    }

    int i = 7;
    return sum == 10 && i == 7 ? 0 : 1;
}
