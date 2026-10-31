/* Linux AMD IOMMU ivrs_ioapic_quirks[][3]: only the outer dimension is
 * inferred from its designated initializer, the inner bound is fixed. */
struct Quirk {
    unsigned char id;
    unsigned int device;
};

static const struct Quirk quirks[][3] = {
    [0] = {
        { .id = 4, .device = 0xa0 },
        { .id = 5, .device = 2 },
        {}
    },
    [2] = {
        { .id = 32, .device = 0xa0 },
        {}
    },
    {}
};

int main(void) {
    if (sizeof(quirks) != 4U * 3U * sizeof(struct Quirk) ||
        quirks[0][0].id != 4U || quirks[0][1].device != 2U ||
        quirks[1][0].id != 0U || quirks[2][0].id != 32U ||
        quirks[3][0].device != 0U)
        return 1;
    return 0;
}
