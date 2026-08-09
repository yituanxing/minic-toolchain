typedef void(callback_type)(int);

void install_callback(callback_type *callback);

static void callback(int value) {
    (void)value;
}

int main(void) {
    install_callback(callback);
    return 0;
}
