const char *envp_init[4] = {"HOME=/", "TERM=linux", ((void *)0), 0};
static const char *argv_init[3] = {"init", ((void *)0), 0};

static const char *local_name(int index) {
    static const char *const names[] = {"first", ((void *)0), 0};
    return names[index];
}

int main(void) {
    return envp_init[0][0] == 'H' && argv_init[0][0] == 'i' && local_name(0)[0] == 'f' ? 0 : 1;
}
