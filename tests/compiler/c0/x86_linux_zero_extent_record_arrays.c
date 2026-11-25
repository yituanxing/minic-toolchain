/* GNU defconfig may leave a feature-gated table without any entries.
 * The declaration is a defined zero-sized object, not a missing alias. */
struct minic_gss_enctype {
    unsigned int etype;
    const char *name;
    unsigned long marker;
};

static const struct minic_gss_enctype supported_gss_krb5_enctypes[] = {};

struct minic_hda_config {
    unsigned long id;
    int flags;
};

static const struct minic_hda_config config_table[] = {};
static const struct minic_hda_config nested_config[][3] = {};

unsigned long minic_zero_record_array_extents(void) {
    return sizeof(supported_gss_krb5_enctypes) +
           sizeof(config_table) + sizeof(nested_config);
}
