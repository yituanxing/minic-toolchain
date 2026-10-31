#include <stdio.h>
#include <string.h>

extern volatile int runtime_bias;
extern int runtime_transform(int value);
extern const char *runtime_message(void);

int main(void) {
    const char *message = runtime_message();
    int value = runtime_transform(9);
    size_t length = strlen(message);
    int ok = value == 74 && runtime_bias == 11 && length == 10 &&
             strcmp(message, "gnu-c-only") == 0;

    printf("MINIAS_GCC_RUNTIME value=%d bias=%d length=%zu status=%s\n",
           value, runtime_bias, length, ok ? "PASS" : "FAIL");
    return ok ? 0 : 71;
}
