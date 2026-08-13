extern int linker_start[];

static char *names[] __attribute__((section(".init.data"))) = {
    "alpha", "beta", ((void *)0),
};

static int *levels[] __attribute__((section(".init.data"))) = {
    linker_start,
};

int main(void) {
    return 0;
}
