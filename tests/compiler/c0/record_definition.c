struct AES_ctx {
    int RoundKey[176];
    int *Next;
    void *(*alloc)(long unsigned int size);
    void (*release)(void *ptr);
};

int main(void)
{
    return 0;
}
