static char *names[] = {"alpha", "beta", ((void *)0)};

int main(void) {
    return names[0][0] == 'a' ? 0 : 1;
}
