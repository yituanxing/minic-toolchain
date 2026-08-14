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

int linux_siginfo_bound(void) {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    return (int)sizeof(dummy);
}

struct RecordBound {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    int tail;
};

int linux_siginfo_record_bound(void) {
    return (int)sizeof(((struct RecordBound *)0)->dummy);
}

int main(void) {
    return multiplied_size() == 1048576 && grouped_size() == 35 && divided_size() == 44 &&
                   linux_siginfo_bound() == 8 && linux_siginfo_record_bound() == 8
               ? 0
               : 1;
}
