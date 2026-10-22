/* An ordinary mismatched callback must stay rejected. */
struct MiniSockaddr { int family; };
typedef union {
    struct MiniSockaddr *sa;
    void *opaque;
} MiniSockaddrArg __attribute__((__transparent_union__));
int wrong_getter(int fd, double wrong, unsigned *length)
{
    (void)fd; (void)wrong; (void)length;
    return -1;
}
int consumer(int (*getter)(int, struct MiniSockaddr *, unsigned *))
{
    return getter(0, 0, 0);
}
int testcase(void)
{
    return consumer(wrong_getter);
}
