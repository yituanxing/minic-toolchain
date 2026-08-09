typedef unsigned long size_type;

int main(void) {
    int sum = 0;
    for (size_type i = 0; i < 4; i++) {
        sum += (int)i;
    }
    return sum == 6 ? 0 : 1;
}
