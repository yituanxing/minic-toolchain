extern int external_count;
int external_count = 7;

long long external_wide = 11LL;
unsigned long loops_per_jiffy = (1 << 12);
unsigned long external_payload_wide = (1UL << 40);
static const unsigned long long internal_runtime_limit = ((1ULL << (64 - 20)) - 1) * 1000L;
struct WidePayloadRecord {
    unsigned long payload;
};
static const struct WidePayloadRecord internal_wide_record = {
    .payload = (1UL << 40),
};
static int internal_folded = (3 + 5) * 2;

int main(void) {
    return external_count == 7 && external_wide == 11LL && loops_per_jiffy == 4096UL &&
                   external_payload_wide == (1UL << 40) &&
                   internal_runtime_limit == 17592186044415000ULL &&
                   internal_wide_record.payload == (1UL << 40) && internal_folded == 16
               ? 0
               : 1;
}
