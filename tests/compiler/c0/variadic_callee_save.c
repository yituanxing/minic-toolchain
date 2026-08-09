void *__minic_va_start(void);
int minic_verify_va_list(void *arguments);

static int check_arguments(int fixed, const char *tag, ...) {
    void *arguments;

    arguments = __minic_va_start();
    return fixed == 11 ? minic_verify_va_list(arguments) : 40;
}

int main(void) {
    return check_arguments(11, "x", 22, 3.5);
}
