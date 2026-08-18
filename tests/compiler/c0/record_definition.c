struct AES_ctx {
    int RoundKey[176];
    int *Next;
};

typedef struct {
    const unsigned char *json;
    unsigned long position;
} error;

struct pointer_array_fields {
    char (*zero)[0];
    char (*one)[1];
    const unsigned char (*two)[2];
};

int main(void)
{
    return 0;
}
