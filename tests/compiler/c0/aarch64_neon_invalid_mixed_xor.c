typedef __Uint64x2_t vector128_u64;
typedef __Uint8x16_t vector128_u8;
static vector128_u64 wide, result;
static vector128_u8 bytes;
void invalid_mixed_lane_xor(void) { result = wide ^ bytes; }
