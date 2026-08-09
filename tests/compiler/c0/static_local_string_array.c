static int first_byte(void) {
    static const char end[] = "abc";
    return end[0];
}

int main(void) {
    return first_byte() == 'a' ? 0 : 1;
}
