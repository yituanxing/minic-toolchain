/* Linux EFI and block-layer CoreType ingress contracts, kept free of sysroot.
 * Distinct source function declarators below have one execution signature;
 * aligned records and pointer-to-qualified records retain their own layout. */
typedef struct __attribute__((packed)) Guid {
    unsigned char bytes[16];
} Guid;
typedef Guid EfiGuid __attribute__((aligned(4)));

typedef struct Node Node;
typedef void DoneCallback(Node *);
struct Node {
    DoneCallback *done;
    const Node *next;
    EfiGuid guid;
};

extern int bytes_equal(const void *, const void *, unsigned long);

int guid_compare(EfiGuid left, EfiGuid right) {
    return bytes_equal(&left, &right, sizeof(EfiGuid));
}

void guid_copy(EfiGuid *dst, const EfiGuid *src) {
    *dst = *src;
}

EfiGuid guid_roundtrip(EfiGuid input) {
    EfiGuid copy = input;
    guid_copy(&copy, &input);
    return copy;
}

static void on_done(Node *node) {
    node->done = (DoneCallback *)0;
}

void wire_node(Node *node) {
    /* Field type and function-declaration type are imported independently. */
    node->done = on_done;
    node->next = node; /* pointer-to-record -> pointer-to-const-record */
}

void run_node(Node *node) {
    if (node->done) node->done(node);
}

/* These MUST remain different ABI signatures when canonicalizing imported
 * function types: fixed and variadic are not interchangeable. */
typedef int FixedFn(int);
typedef int VariadicFn(int, ...);
_Static_assert(!__builtin_types_compatible_p(FixedFn *, VariadicFn *),
               "variadic function pointers must remain different");
