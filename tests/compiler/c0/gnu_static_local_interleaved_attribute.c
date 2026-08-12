struct LockdepLike {
    int key;
    unsigned long state;
};

static int record_value(void)
{
    static struct LockdepLike __attribute__((__unused__)) map = {};
    return map.key + (int)map.state;
}

static int scalar_value(void)
{
    static int __attribute__((__unused__)) value = 7;
    return value;
}

int main(void)
{
    return record_value() == 0 && scalar_value() == 7 ? 0 : 1;
}
