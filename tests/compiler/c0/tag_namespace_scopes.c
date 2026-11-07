/* The C tag namespace is shared by struct, union and enum, but nested
 * scopes may shadow an outer tag with a different tag kind. */
enum changing_tag { ENUM_GLOBAL = 17 };
struct record_to_enum { int field; };
union union_to_record { int field; unsigned int alternative; };

static int enum_to_struct_to_enum(void) {
    struct changing_tag { int field; };
    struct changing_tag local;
    local.field = 18;
    {
        enum changing_tag { ENUM_NESTED = 19 };
        enum changing_tag nested = ENUM_NESTED;
        local.field += (int)nested;
    }
    local.field += (int)sizeof(struct changing_tag);
    return local.field;
}

static int struct_to_enum(void) {
    enum record_to_enum { ENUM_REPLACEMENT = 23 };
    enum record_to_enum local = ENUM_REPLACEMENT;
    return (int)local;
}

static int union_to_struct(void) {
    struct union_to_record { int field; };
    struct union_to_record local;
    local.field = 29;
    return local.field;
}

static int restore_file_tags(void) {
    enum changing_tag value = ENUM_GLOBAL;
    struct record_to_enum object;
    union union_to_record variable;
    object.field = 31;
    variable.field = 37;
    return (int)value + object.field + variable.field;
}

static int different_kind_forward(void) {
    struct changing_tag;
    struct changing_tag { int field; };
    struct changing_tag local;
    local.field = 41;
    return local.field;
}

int main(void) {
    return enum_to_struct_to_enum() == 41 && struct_to_enum() == 23 &&
           union_to_struct() == 29 && restore_file_tags() == 85 &&
           different_kind_forward() == 41 ? 0 : 1;
}
