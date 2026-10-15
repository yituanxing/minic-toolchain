#if !defined(__GNUC__) || !defined(__GNUC_MINOR__)
#error Mini driver must advertise its supported GNU attribute surface
#endif

#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
#define MINI_DRIVER_PACKED __attribute__((__packed__))
#else
#define MINI_DRIVER_PACKED
#endif

struct DriverPackedProbe {
    unsigned char tag;
    unsigned int value;
} MINI_DRIVER_PACKED;

_Static_assert(sizeof(struct DriverPackedProbe) == 5,
               "Mini driver GNU predefines must preserve packed attributes");

int driver_gnu_predefine_probe(void) {
    return (int)sizeof(struct DriverPackedProbe);
}
