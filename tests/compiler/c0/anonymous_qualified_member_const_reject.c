struct Vma { int x; };
struct VmFault {
    const struct { struct Vma *vma; };
};
void invalid_write(struct VmFault *vmf, struct Vma *other) {
    vmf->vma = other;
}
