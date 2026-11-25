extern unsigned long kvm_vector_storage_signature(void);

int main(void) {
    return kvm_vector_storage_signature() == 80UL ? 0 : 1;
}
