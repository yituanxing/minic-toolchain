struct empty_record { };

static struct empty_record *zero_stride(struct empty_record *pointer, long index) {
    return pointer + index;
}

int main(void) {
    return 0;
}
