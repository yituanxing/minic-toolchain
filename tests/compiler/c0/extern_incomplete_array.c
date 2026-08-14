extern const char message[];

int read_second(void) {
    return message[1];
}

const char message[] = "ab" "cd";

int main(void) {
    return sizeof(message) == 5 && read_second() == 'b' ? 0 : 1;
}
