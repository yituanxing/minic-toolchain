/* Linux i915 power maps contain file-scope compound-literal record arrays
 * with two pointer relocations per row and an anonymous union of records. */
enum minic_well_id { MINIC_WELL_ALWAYS_ON = 1, MINIC_WELL_BXT = 7 };

struct minic_domain_list {
    const unsigned int *list;
    unsigned char count;
};

struct minic_well_instance {
    const char *name;
    const struct minic_domain_list *domains;
    enum minic_well_id id;
    union {
        struct { unsigned char index; } vlv;
        struct { int phy; } bxt;
        struct { unsigned char index; } hsw;
    };
};

static const unsigned int minic_domains[] = { 2, 4, 6 };
static const struct minic_domain_list minic_domain_set = { minic_domains, 3 };

#define MINIC_WELL_ROWS(...) ((const struct minic_well_instance[]) { __VA_ARGS__ })

static const struct minic_well_instance *minic_compound_wells =
    MINIC_WELL_ROWS(
        { .name = "always-on", .domains = &minic_domain_set,
          .id = MINIC_WELL_ALWAYS_ON },
        { .name = "bxt", .domains = &minic_domain_set,
          .id = MINIC_WELL_BXT, .bxt.phy = 0x1234 }
    );

int minic_nested_union_compound_result(void) {
    return (minic_compound_wells[0].id == MINIC_WELL_ALWAYS_ON &&
            minic_compound_wells[0].vlv.index == 0 &&
            minic_compound_wells[1].id == MINIC_WELL_BXT &&
            minic_compound_wells[1].bxt.phy == 0x1234 &&
            minic_compound_wells[1].domains->list[2] == 6 &&
            minic_compound_wells[0].name[0] == 'a' &&
            minic_compound_wells[1].name[1] == 'x') ? 0 : 91;
}

int main(void) { return minic_nested_union_compound_result(); }
