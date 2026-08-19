static unsigned long ids[1];

struct map_ops {
    int *map_btf_id;
};

static struct map_ops ops = {
    .map_btf_id = &ids[0],
};

int main(void) {
    return ops.map_btf_id != (int *)0;
}
