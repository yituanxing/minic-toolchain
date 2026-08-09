static void callback(void) {
}

int accept_callback(void (*function)(void));

int main(void) {
    return accept_callback(callback);
}
