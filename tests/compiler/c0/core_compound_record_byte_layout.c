/* Linux 6.6 UFS compile-time layout guards.  All ten cases must fold using
 * DataLayout (including nested anonymous union/struct bitfields), with no
 * synthetic compiletime_assert references emitted by MiniC. */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;

struct request_desc_header {
    u8 cci;
    u8 ehs_length;
    u8 reserved2 : 7;
    u8 enable_crypto : 1;
    u8 interrupt : 1;
    u8 data_direction : 2;
    u8 reserved1 : 1;
    u8 command_type : 4;
    u32 dunl;
    u8 ocs;
    u8 cds;
    u16 ldbc;
    u32 dunu;
};

struct utp_upiu_header {
    union {
        struct { u32 dword_0, dword_1, dword_2; };
        struct {
            u8 transaction_code, flags, lun, task_tag;
            u8 command_set_type : 4;
            u8 iid : 4;
            union { u8 tm_function; u8 query_function; };
            u8 response, status, ehs_length, device_information;
            u16 data_segment_length;
        };
    };
};

extern void invalid_compound_layout(void)
    __attribute__((error("compound record layout did not fold")));

int compound_record_layout_guards(void) {
    if (((u8 *)&(struct request_desc_header){ .cci = 3 })[0] != 3)
        invalid_compound_layout();
    if (((u8 *)&(struct request_desc_header){ .ehs_length = 2 })[1] != 2)
        invalid_compound_layout();
    if (((u8 *)&(struct request_desc_header){ .enable_crypto = 1 })[2] != 0x80)
        invalid_compound_layout();
    if (((u8 *)&(struct request_desc_header){
            .command_type = 5, .data_direction = 3, .interrupt = 1 })[3] !=
        ((5 << 4) | (3 << 1) | 1))
        invalid_compound_layout();
    if (((u32 *)&(struct request_desc_header){ .dunl = 0xdeadbeefU })[1] !=
        0xdeadbeefU)
        invalid_compound_layout();
    if (((u8 *)&(struct request_desc_header){ .ocs = 4 })[8] != 4)
        invalid_compound_layout();
    if (((u8 *)&(struct request_desc_header){ .cds = 5 })[9] != 5)
        invalid_compound_layout();
    if (((u32 *)&(struct request_desc_header){ .dunu = 0x0badcafeU })[3] !=
        0x0badcafeU)
        invalid_compound_layout();
    if (((u8 *)&(struct utp_upiu_header){ .iid = 0xf })[4] != 0xf0)
        invalid_compound_layout();
    if (((u8 *)&(struct utp_upiu_header){ .command_set_type = 0xf })[4] != 0x0f)
        invalid_compound_layout();
    return 10;
}

/* Dynamic initializers must remain dynamic, including after a prior static
 * compound-literal proof in the same translation unit. */
int compound_record_dynamic_field(u8 value) {
    return ((u8 *)&(struct request_desc_header){ .cci = value })[0];
}

int main(void) {
    return compound_record_layout_guards() == 10 &&
           compound_record_dynamic_field(7) == 7 &&
           compound_record_dynamic_field(13) == 13 ? 0 : 1;
}
