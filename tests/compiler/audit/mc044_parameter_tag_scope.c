/* A C tag introduced in a function parameter list does not escape the
 * prototype scope; a function definition's parameter tags remain visible
 * in the body, but not in the enclosing file scope. */
#if MC044_VARIANT == 1
void prototype(struct OnlyInside { int a; } value);
struct OnlyInside { char b; };
int main(void) { return sizeof(struct OnlyInside) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 2
int definition(struct BodyTag { int a; } value) {
    return value.a + (int)(sizeof(struct BodyTag) - sizeof(value));
}
struct BodyTag { char b; };
int main(void) { return sizeof(struct BodyTag) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 3
typedef int (*Callback)(struct HiddenPointer { int a; } *value);
struct HiddenPointer { char b; };
int main(void) { return sizeof(struct HiddenPointer) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 4
void enum_prototype(enum HiddenEnum { INNER = 2 } value);
enum HiddenEnum { OUTER = 4 };
int main(void) { return OUTER + 3; }
#elif MC044_VARIANT == 5
typedef int (*NestedCallback)(int (*nested)(struct DeepTag { int a; } *value));
struct DeepTag { char b; };
int main(void) { return sizeof(struct DeepTag) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 6
int enum_definition(enum InsideDef { ONLY = 5 } x) { return x + ONLY - 5; }
enum InsideDef { OUTSIDE = 2 };
int main(void) { return sizeof(enum InsideDef) == sizeof(int) ? 7 : 1; }
#elif MC044_VARIANT == 7
void nested_parameter(void (*handler)(struct Inner { int a; } *),
                      struct Inner { char b; } *outer);
struct Inner { double c; };
int main(void) { return sizeof(struct Inner) == sizeof(double) ? 7 : 1; }
#elif MC044_VARIANT == 8
int (*callback_object)(struct ObjectInner { int a; } *value);
struct ObjectInner { char b; };
int main(void) { return sizeof(struct ObjectInner) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 9
typedef int CallbackType(struct TypeInner { int a; } *value);
struct TypeInner { char b; };
int main(void) { return sizeof(struct TypeInner) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 10
/* Two separately defined prototype-scope tags are not one shared type. */
int incompatible(struct PrototypeOnly { int a; } *value);
int incompatible(struct PrototypeOnly { int a; } *value);
#elif MC044_VARIANT == 11
int (*returns_callback(struct ReturnTag { int a; } value))(void);
struct ReturnTag { char b; };
int main(void) { return sizeof(struct ReturnTag) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 12
typedef int (*(*CallbackFactory)(struct FactoryTag { int a; } value))(void);
struct FactoryTag { char b; };
int main(void) { return sizeof(struct FactoryTag) == 1U ? 7 : 1; }
#elif MC044_VARIANT == 13
int main(void) {
    void local_prototype(struct LocalTag { int a; } value);
    struct LocalTag { char b; };
    return sizeof(struct LocalTag) == 1U ? 7 : 1;
}
#elif MC044_VARIANT == 14
int main(void) {
    extern void local_extern(struct LocalExternTag { int a; } value);
    struct LocalExternTag { char b; };
    return sizeof(struct LocalExternTag) == 1U ? 7 : 1;
}
#elif MC044_VARIANT == 15
int main(void) {
    typedef int (*LocalCallback)(struct LocalNestedTag { int a; } *value);
    struct LocalNestedTag { char b; };
    return sizeof(struct LocalNestedTag) == 1U ? 7 : 1;
}
#else
#error MC044_VARIANT must be 1..15
#endif
