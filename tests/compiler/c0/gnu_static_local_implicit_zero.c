struct lock_class_key {
    unsigned long owner;
    unsigned int state;
};

extern void consume_address(void *value);

void linux_static_key_shape(void)
{
    static struct lock_class_key key;
    static int scalar;
    static int *pointer;
    static int values[3];

    consume_address(&key);
    consume_address(&scalar);
    consume_address(&pointer);
    consume_address(&values[0]);
}
