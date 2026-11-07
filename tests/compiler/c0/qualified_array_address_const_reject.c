struct Flexible {
    unsigned long prefix;
    unsigned char bytes[];
};
void cannot_modify_const_flexible_array(const struct Flexible *owner) {
    owner->bytes[0] = 12U;
}
