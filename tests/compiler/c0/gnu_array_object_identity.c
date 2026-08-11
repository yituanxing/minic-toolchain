struct CpuMask {
    unsigned long bits[1];
};
struct FixedArrayHolder {
    int head;
    unsigned long values[4];
};
struct FlexibleArrayHolder {
    int head;
    unsigned long values[];
};
unsigned long fixed_member_size(struct FixedArrayHolder *holder) { return sizeof(holder->values); }
unsigned long fixed_member_address_pointee_size(struct FixedArrayHolder *holder) { return sizeof(*(&holder->values)); }
unsigned long fixed_member_typeof_size(struct FixedArrayHolder *holder) { return sizeof(typeof(holder->values)); }
unsigned long fixed_member_index(struct FixedArrayHolder *holder) { return holder->values[2]; }
unsigned long *fixed_member_decay(struct FixedArrayHolder *holder) { return holder->values; }
struct CpuMask *linux_flexible_array_shape(struct FlexibleArrayHolder *holder) { return (struct CpuMask *)&holder->values; }
unsigned long local_array_address_pointee_size(void) { unsigned long values[3]; return sizeof(*(&values)); }
unsigned long local_array_typeof_size(void) { unsigned long values[3]; return sizeof(typeof(values)); }
