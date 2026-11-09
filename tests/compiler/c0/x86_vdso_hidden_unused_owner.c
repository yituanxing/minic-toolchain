long vdso_hidden_required_data = 37;
long vdso_default_preemptible_data = 2;

long vdso_hidden_required_function(long input) {
    return input + 3;
}

extern long minic_x86_vdso_hidden_probe(long);

int main(void) {
    return minic_x86_vdso_hidden_probe(0) == 42 ? 0 : 1;
}
