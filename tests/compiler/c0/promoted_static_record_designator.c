static int btf_id;

struct proto {
    int ret_type;
    union {
        struct {
            int arg1_type;
            int *arg1_btf_id;
        };
        long storage[2];
    };
};

static struct proto value = {
    .ret_type = 1,
    .arg1_type = 2,
    .arg1_btf_id = &btf_id,
};

int main(void) {
    return value.ret_type != 1 || value.arg1_type != 2 || value.arg1_btf_id != &btf_id;
}
