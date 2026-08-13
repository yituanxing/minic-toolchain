typedef struct Link {
    struct Link *next;
    struct Link *prev;
} Link;

typedef struct Inner {
    int first;
    unsigned int magic;
    int second;
    void *owner;
    Link link;
} Inner;

typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

static Outer value = {
    3,
    (Inner) {
        .magic = 0xdead4ead,
        .second = 7,
        .owner = (void *)-1L,
        .link = { &value.inner.link, &value.inner.link },
    },
};

int main(void)
{
    return value.tag == 3 && value.inner.first == 0 && value.inner.magic == 0xdead4eadU &&
                   value.inner.second == 7 && value.inner.owner == (void *)-1L &&
                   value.inner.link.next == &value.inner.link &&
                   value.inner.link.prev == &value.inner.link
               ? 0
               : 1;
}
