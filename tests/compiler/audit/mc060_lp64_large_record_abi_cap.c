/* MC-060: AArch64 and LoongArch64 should not reject a valid by-value
 * aggregate solely because it has 2049 bytes. Build with -DMC060_SMALL
 * to use 2048 bytes as the control. */
#ifdef MC060_SMALL
#define MC060_BYTES 2048
#else
#define MC060_BYTES 2049
#endif
struct LargeRecord { unsigned char bytes[MC060_BYTES]; };
struct LargeRecord pass_record(struct LargeRecord x) { return x; }
unsigned char inspect_record(struct LargeRecord x) {
    return pass_record(x).bytes[MC060_BYTES - 1];
}
