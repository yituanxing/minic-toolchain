typedef struct Inner {
    int first;
    unsigned int magic;
    int second;
} Inner;

typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

static Outer value = { 3, (Inner) { .magic = 0xdead4ead, .second = 7 } };

int main(void)
{
    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&
                   value.inner.second == 7
               ? 0
               : 1;
}
