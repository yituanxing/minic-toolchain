struct align_tail {
    long head;
    unsigned char tail[];
};

unsigned long alignof_scalar_member(struct align_tail *ptr) {
    return __alignof__(ptr->head);
}

unsigned long alignof_flexible_array_member(struct align_tail *ptr) {
    return __alignof__(ptr->tail);
}

int main(void) {
    struct align_tail *ptr = 0;
    return (int)(alignof_scalar_member(ptr) == 0 || alignof_flexible_array_member(ptr) == 0);
}
