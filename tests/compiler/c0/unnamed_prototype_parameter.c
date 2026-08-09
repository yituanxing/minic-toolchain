int consume(int);
void install(void (*)(void));

static void callback(void) {
}

int main(void) {
    install(callback);
    return consume(0);
}
