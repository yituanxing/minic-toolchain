/* MC-059: independent producer TU for GNU/MiniC ABI interoperability. */
union U64 { double d; unsigned long long bits; };
union OnlyFP { double d; };
struct Nested { unsigned long long tag; union U64 data; };
struct Array { union U64 items[2]; };

union U64 abi_echo(union U64 x) { return x; }
unsigned long long abi_bits(union U64 x) { return x.bits; }
struct Nested abi_nested(struct Nested x) { return x; }
struct Array abi_array(struct Array x) { return x; }
union OnlyFP abi_only_fp(union OnlyFP x) { return x; }
double abi_scalar(double x) { return x + 2.0; }
