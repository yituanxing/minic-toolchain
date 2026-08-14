struct a { int value; };
struct b { int value; };

typedef union {
    struct a **as;
    struct b **bs;
} bridge_arg __attribute__((__transparent_union__));

int sink(bridge_arg arg)
{
    return arg.as != ((void *)0);
}

int bad(char **other)
{
    return sink(other);
}
