const char *bad[1] = {(void *)1};

int main(void) {
    return bad[0] != 0;
}
