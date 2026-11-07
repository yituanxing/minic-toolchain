struct Flexible {
    unsigned long prefix;
    unsigned char bytes[];
};
void incompatible_array_element_pointer(const struct Flexible *owner) {
    const unsigned short (*wrong)[] = &owner->bytes;
    (void)wrong;
}
