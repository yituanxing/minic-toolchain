extern int start_a[];
extern int start_b[];

static int *levels[] __attribute__((section(".init.data"))) = {start_a, start_b};
static char *names[] = {"alpha", "beta", ((void *)0)};

int main(void) {
    return 0;
}
