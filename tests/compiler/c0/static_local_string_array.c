static int __attribute__((used)) first_byte(void) {
    static const char end[] __attribute__((used)) = "abc";
    return end[0];
}

int main(void) {
    return first_byte() == 'a' ? 0 : 1;
}
