/* GNU anonymous record members retain the qualifiers of the directly written
 * record specifier.  A typedef-only or tagged bare declaration remains
 * memberless even if the spelling carries the same qualifiers. */
typedef struct { unsigned long ignored; } HiddenAlias;

struct Vma { unsigned long tag; };

struct VmFault {
    const struct {
        struct Vma *vma;
        unsigned long address;
        union { unsigned int flags; unsigned int mask; };
    };
    volatile union { unsigned int state; unsigned int state_alias; };
    unsigned long tail;
};

struct DeepVmFault {
    const volatile struct {
        struct {
            struct Vma *vma;
        };
    };
    unsigned long tail;
};

struct ExtensionFault {
    __extension__ const struct { unsigned long value; };
};

struct QualifiedNonmembers {
    const HiddenAlias;
    volatile struct Vma;
    unsigned long tail;
};

static struct Vma vma = { 47UL };
static struct VmFault fault = { .vma = &vma, .address = 0x1234UL,
                               .flags = 6U, .state = 9U, .tail = 19UL };
static struct DeepVmFault deep_fault = { .vma = &vma, .tail = 31UL };
static struct ExtensionFault extension_fault = { .value = 43UL };

int main(void) {
    struct VmFault *vmf = &fault;
    struct DeepVmFault *nested = &deep_fault;
    return vmf->vma->tag != 47UL || vmf->address != 0x1234UL ||
           vmf->mask != 6U || vmf->state_alias != 9U || vmf->tail != 19UL ||
           nested->vma->tag != 47UL || nested->tail != 31UL ||
           extension_fault.value != 43UL ||
           sizeof(struct QualifiedNonmembers) != sizeof(unsigned long) ||
           __builtin_offsetof(struct VmFault, vma) != 0U;
}
