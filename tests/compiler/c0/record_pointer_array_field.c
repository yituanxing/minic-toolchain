struct holder {
    char (*zero)[0];
    char (*one)[1];
    const unsigned char (*two)[2];
};
int main(void) {
    return 0;
}
