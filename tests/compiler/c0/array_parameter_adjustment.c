typedef unsigned char u8;

void generate_random_uuid(u8 uuid[16]);
void generate_random_uuid(u8 *uuid)
{
    uuid[0] = 7;
}

void unnamed_array_parameter(u8 [1 << 4]);
void unnamed_array_parameter(u8 *bytes)
{
    bytes[1] = 9;
}

typedef void (*generator_fn)(u8 bytes[16]);

static int adjusted_size(u8 bytes[16])
{
    _Static_assert(sizeof(bytes) == sizeof(void *), "array parameter adjusts to pointer");
    return (int)bytes[0];
}

int main(void)
{
    u8 bytes[16] = {0};
    generator_fn fn = generate_random_uuid;
    fn(bytes);
    unnamed_array_parameter(bytes);
    return adjusted_size(bytes) == 7 && bytes[1] == 9 ? 0 : 1;
}
