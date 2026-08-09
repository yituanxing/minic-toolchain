char *__minic_va_start(void);
int minic_verify_va_list(char *arguments);

static int check_arguments(int fixed, const char *tag, ...) {
    char *arguments;

    arguments = __minic_va_start();
    return fixed == 11 ? minic_verify_va_list(arguments) : 40;
}

int main(void) {
    return check_arguments(11, "x", 22, 3.5);
}
