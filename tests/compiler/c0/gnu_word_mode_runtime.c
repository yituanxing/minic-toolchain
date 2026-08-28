typedef int register_t __attribute__((__mode__ (__word__)));
typedef unsigned int uregister_t __attribute__((__mode__((__word__))));

_Static_assert(sizeof(register_t) == 8, "signed word mode");
_Static_assert(sizeof(uregister_t) == 8, "unsigned word mode");

int main(void) {
    register_t a = -1;
    uregister_t b = 1;
    return (a < 0 && b == 1) ? 0 : 1;
}
