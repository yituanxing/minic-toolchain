int main(int argc, char **argv) {
    if (argc < 1) {
        return 1;
    }
    if (argv == (void *)0 || argv[0] == (void *)0) {
        return 2;
    }
    return 0;
}
