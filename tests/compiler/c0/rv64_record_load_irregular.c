/* The RV64 Core RECORD_LOAD fallback must copy every byte of a record.
 * Sizes 3/5/6/7 previously loaded only the first byte into a wider slot. */
struct three { unsigned char b[3]; };
struct five { unsigned char b[5]; };
struct six { unsigned char b[6]; };
struct seven { unsigned char b[7]; };
struct nine { unsigned char b[9]; };
struct fifteen { unsigned char b[15]; };

struct three load_three(const struct three *p) { return *p; }
struct five load_five(const struct five *p) { return *p; }
struct six load_six(const struct six *p) { return *p; }
struct seven load_seven(const struct seven *p) { return *p; }
struct nine load_nine(const struct nine *p) { return *p; }
struct fifteen load_fifteen(const struct fifteen *p) { return *p; }
