struct LockdepLike {
    int key;
    unsigned long state;
};

static int record_value(void) {
    static struct LockdepLike __attribute__((__unused__)) map = {};
    return map.key + (int)map.state;
}

static int scalar_value(void) {
    static int __attribute__((__unused__)) value = 7;
    return value;
}

static int section_value(void) {
    static _Bool __attribute__((__section__(".data..once"))) already_done;
    static int __attribute__((section(".data.localpair"))) first, second;
    already_done = 1;
    first = 3;
    second = 4;
    return (int)already_done + first + second;
}

int main(void) {
    return record_value() == 0 && scalar_value() == 7 && section_value() == 8 ? 0 : 1;
}
