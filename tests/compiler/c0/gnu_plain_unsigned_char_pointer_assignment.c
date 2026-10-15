static char *pack_upper(char *cursor) {
    return cursor;
}

int main(void) {
    unsigned char storage[2] = {0, 0};
    unsigned char *cursor = storage;
    char *plain = (char *)cursor;

    cursor = pack_upper(plain);
    plain = cursor;
    return plain == (char *)storage ? 0 : 1;
}
