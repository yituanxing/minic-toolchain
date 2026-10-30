typedef struct {
    const unsigned char *json;
    unsigned long position;
} error;

static error global_error = {((void *)0), 0};
/* This fixture asserts exact physical ELF array data, so explicitly retain
 * the otherwise unreferenced static const object under dead-object DCE. */
static const int table[4] __attribute__((used)) = {0x63, 0X7c, 1,};

int main(void)
{
    return 0;
}
