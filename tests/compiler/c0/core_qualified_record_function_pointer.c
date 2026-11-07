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

/* Nested const on the pointer object must not discard the ABI alignment
 * of its pointee during C -> Core type import.  These field accesses also
 * exercise the exact FIELD_ADDRESS Core identity contract. */
typedef struct AlignedGuidSlot {
    EfiGuid *pointer;
} AlignedGuidSlot;

EfiGuid *load_guid_from_const_slot(const AlignedGuidSlot *slot) {
    return slot->pointer;
}

typedef struct ReadOnlyGuidSlot {
    const EfiGuid *const pointer;
} ReadOnlyGuidSlot;

const EfiGuid *load_const_guid_from_slot(const ReadOnlyGuidSlot *slot) {
    return slot->pointer;
}

/* An aligned integer typedef is ABI-relevant on a fixed call signature,
 * but its storage alignment is not part of the scalar LOAD result. */
typedef unsigned long long AlignedU64 __attribute__((aligned(16)));
extern AlignedU64 echo_aligned_u64(AlignedU64 value);

AlignedU64 call_aligned_u64(AlignedU64 input) {
    AlignedU64 copy = input;
    return echo_aligned_u64(copy);
}

/* Linux mpt3sas writes through an aligned integer typedef and then uses
 * |= on that same field. The storage address retains the ABI alignment,
 * whereas Core LOAD yields an unaligned scalar SSA value. Exercise both
 * direct and aggregate-member compound updates across every Target. */
typedef struct ResponseFrame {
    AlignedU64 address;
} ResponseFrame;

unsigned long long update_aligned_response(ResponseFrame *frame,
                                           unsigned long long high_bits) {
    frame->address |= high_bits;
    frame->address += 1ULL;
    return frame->address;
}

typedef double AlignedDouble __attribute__((aligned(16)));

double update_aligned_float(AlignedDouble *result, double increment) {
    *result += increment;
    return *result;
}

_Static_assert(!__builtin_types_compatible_p(EfiGuid *, const EfiGuid *),
               "discarding pointee const is not allowed by C types");

/* These MUST remain different ABI signatures when canonicalizing imported
 * function types: fixed and variadic are not interchangeable. */
typedef int FixedFn(int);
typedef int VariadicFn(int, ...);
_Static_assert(!__builtin_types_compatible_p(FixedFn *, VariadicFn *),
               "variadic function pointers must remain different");
