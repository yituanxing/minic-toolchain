typedef struct { unsigned int value; } MiniUserId;
typedef struct { unsigned int value; } MiniGroupId;
typedef struct { double value; } MiniReal;
typedef struct { void *value; } MiniPointer;

extern unsigned int mini_va_record_sum(unsigned int tag, ...);
extern unsigned int mini_va_record_many(unsigned int tag, ...);
extern double mini_va_record_real(unsigned int tag, ...);
extern unsigned long mini_va_record_pointer(unsigned int tag, ...);

int main(void) {
    MiniUserId uid = { 25U };
    MiniGroupId gid = { 7U };
    MiniReal v = { 1.0 };
    MiniPointer pointer = { (void *)&uid };
    if (mini_va_record_sum(10U, uid, gid) != 42U) return 1;
    if (mini_va_record_many(3U, uid, uid, uid, uid, uid,
                            uid, uid, uid, uid) != 228U) return 2;
    if (mini_va_record_real(2U, v, v, v, v, v,
                            v, v, v, v) != 11.0) return 3;
    if (mini_va_record_pointer(7U, pointer) !=
        (unsigned long)(void *)&uid + 7U) return 4;
    return 0;
}
