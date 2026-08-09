int multiplied_size(void) {
    char multiplied[1024 * 1024];
    return (int)sizeof(multiplied);
}

int grouped_size(void) {
    unsigned char grouped[(2 + 3) * (8 - 1)];
    return (int)sizeof(grouped);
}

int divided_size(void) {
    int divided[(24 / 3) + (7 % 4)];
    return (int)sizeof(divided);
}

int main(void) {
    return multiplied_size() == 1048576 && grouped_size() == 35 && divided_size() == 44 ? 0 : 1;
}
