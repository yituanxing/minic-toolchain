typedef unsigned int u32;

static const u32 btf_ids[1];

struct map_ops {
    int *map_btf_id;
};

static struct map_ops ops = {
    .map_btf_id = &btf_ids[0],
};

int main(void) {
    return ops.map_btf_id != (int *)0;
}
