typedef unsigned long core_m29_u64;
typedef unsigned int core_m29_u32;

union CoreM29Union {
    core_m29_u64 ll;
    struct {
        core_m29_u32 low;
        core_m29_u32 high;
    } l;
};

struct CoreM29WideRecord {
    core_m29_u64 a;
    core_m29_u64 b;
    core_m29_u64 c;
};

static core_m29_u64 core_m29_mul_u32_u32(core_m29_u32 a, core_m29_u32 b) {
    return (core_m29_u64)a * b;
}

core_m29_u64 core_m29_union_roundtrip(core_m29_u64 value) {
    union CoreM29Union u;

    u.ll = value;
    return u.ll;
}

core_m29_u32 core_m29_union_low(core_m29_u64 value) {
    union CoreM29Union u;

    u.ll = value;
    return u.l.low;
}

core_m29_u64 core_m29_record_24(core_m29_u64 value) {
    struct CoreM29WideRecord r;

    r.a = value;
    r.b = value + 1UL;
    r.c = value + 2UL;
    return r.a + r.b + r.c;
}

core_m29_u64 core_m29_mul_u64_u32_div(core_m29_u64 a,
                                       core_m29_u32 mul,
                                       core_m29_u32 divisor) {
    union CoreM29Union u;
    union CoreM29Union rl;
    union CoreM29Union rh;

    u.ll = a;
    rl.ll = core_m29_mul_u32_u32(u.l.low, mul);
    rh.ll = core_m29_mul_u32_u32(u.l.high, mul) + rl.l.high;
    rl.l.high = ({
        core_m29_u32 base = divisor;
        core_m29_u32 rem;
        rem = ((core_m29_u64)rh.ll) % base;
        rh.ll = ((core_m29_u64)rh.ll) / base;
        rem;
    });
    ({
        core_m29_u32 base = divisor;
        core_m29_u32 rem;
        rem = ((core_m29_u64)rl.ll) % base;
        rl.ll = ((core_m29_u64)rl.ll) / base;
        rem;
    });
    rl.l.high = rh.l.low;
    return rl.ll;
}
