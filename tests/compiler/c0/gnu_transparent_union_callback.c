/* Original BusyBox libbb/xconnect.c meets glibc getsockname:
 * a function whose second parameter is a GNU transparent-union pointer
 * is passed where the consumer declares the union's FIRST pointer member.
 * This must preserve the C function-pointer ABI without accepting unrelated
 * incompatible signatures. */
#include <stdio.h>
struct MiniSockaddr { int family; };
typedef union {
    struct MiniSockaddr *sa;
    void *opaque;
} MiniSockaddrArg __attribute__((__transparent_union__));

static int read_address(int fd, MiniSockaddrArg addr, unsigned *length)
{
    if (addr.sa == 0 || length == 0 || *length != 4U)
        return -1;
    ++*length;
    return fd + addr.sa->family + (int)*length;
}

static int run_callback(int fd,
                        int (*getter)(int, struct MiniSockaddr *, unsigned *))
{
    struct MiniSockaddr addr = {17};
    unsigned length = 4U;
    int result = getter(fd, &addr, &length);
    return result + (int)length;
}

int main(void)
{
    int result = run_callback(7, read_address);
    if (result != 34) {
        printf("bad=%d\n", result);
        return 1;
    }
    puts("transparent-union-callback: 34");
    return 0;
}
