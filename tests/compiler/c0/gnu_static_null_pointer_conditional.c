/* Linux nf_conntrack_sip: GNU C constant integer fields can use a pointer
 * null constant as the condition of a compile-time conditional expression. */
struct minic_sip_header {
    const char *name;
    unsigned long length;
};
static const struct minic_sip_header minic_sip_headers[] = {
    { .name = "CSeq", .length = (((void *)0)) ? sizeof(((void *)0)) - 1 : 0 },
    { .name = "From", .length = ("f") ? sizeof("f") - 1 : 0 }
};

int minic_sip_header_result(void) {
    return minic_sip_headers[0].length == 0 &&
           minic_sip_headers[1].length == 1 ? 0 : 71;
}

int main(void) { return minic_sip_header_result(); }
