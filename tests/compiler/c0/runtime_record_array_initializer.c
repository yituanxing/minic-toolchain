typedef unsigned char u8;
typedef struct {
    u8 b[16];
} guid_t;

static int consume_guid(guid_t *guid) {
    return guid->b[0] + guid->b[15];
}

int linux_guid_compound_literal(void) {
    return consume_guid(&(guid_t){{(0x8be4df61) & 0xff,
                                   ((0x8be4df61) >> 8) & 0xff,
                                   ((0x8be4df61) >> 16) & 0xff,
                                   ((0x8be4df61) >> 24) & 0xff,
                                   (0x93ca) & 0xff,
                                   ((0x93ca) >> 8) & 0xff,
                                   (0x11d2) & 0xff,
                                   ((0x11d2) >> 8) & 0xff,
                                   0xaa,
                                   0x0d,
                                   0x00,
                                   0xe0,
                                   0x98,
                                   0x03,
                                   0x2b,
                                   0x8c}});
}
