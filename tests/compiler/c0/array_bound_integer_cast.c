enum Code {
    CODE_LAST = 7
};

int main(void) {
    int table[(int)(CODE_LAST) + 1];
    unsigned char small[(unsigned char)(260)];
    return sizeof(table) == 32 && sizeof(small) == 4 ? 0 : 1;
}
