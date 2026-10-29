/* Real GNU Linux 6.6 frontend patterns: x86 csum, AMD IBS and APIC static
   calls, pgtable zero-length stack arrays and sectioned static locals. */
union RawWord {
    unsigned long value;
    unsigned char bytes[8];
};

static int callback(int n) { return n + 7; }
static int (*callback_slot)(int) = callback;
extern typeof(*callback_slot) static_call_trampoline;

static unsigned long union_scalar_cast(unsigned long bits) {
    union RawWord converted = (union RawWord)bits;
    return converted.value;
}

static int function_designator_decay(void) {
    typeof(&static_call_trampoline) fn = (*callback_slot);
    return fn(5);
}

static unsigned long gnu_array_pointer_cast(const void *ptr) {
    return sizeof(*(const char (*)[32])ptr);
}

static unsigned long local_zero_extent(void) {
    unsigned long slots[0];
    return sizeof(slots);
}

static unsigned long sectioned_static_object(void) {
    static const __attribute__((section(".rodata.gnu.front")))
        unsigned long values[] = { 17UL, 23UL };
    return values[1];
}


struct SampleRecord { int value; };
static struct SampleRecord record_value(int n) {
    struct SampleRecord result = { n };
    return result;
}
static unsigned long inferred_record_array(void) {
    struct SampleRecord elements[] = {
        record_value(5),
        ({ struct SampleRecord result = record_value(7); result; }),
    };
    return sizeof(elements) + (unsigned long)(elements[0].value + elements[1].value);
}

int main(void) {
    char bytes[32];
    if (union_scalar_cast(0x12345678UL) != 0x12345678UL) return 1;
    if (function_designator_decay() != 12) return 2;
    if (gnu_array_pointer_cast(bytes) != 32) return 3;
    if (local_zero_extent() != 0) return 4;
    if (sectioned_static_object() != 23) return 5;
    if (inferred_record_array() != 2 * sizeof(struct SampleRecord) + 12) return 6;
    return 0;
}
