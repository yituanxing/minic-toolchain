/* Genuine GNU C producer control for the separate malformed Core API tests.
 * Scalar enum values and function parameter/return types require registered
 * integer-representation metadata during normal Core lowering. */
enum Flavor { FLAVOR_A = 1, FLAVOR_B = 41 };

static enum Flavor identity(enum Flavor value) { return value; }

int main(void) {
    enum Flavor value = FLAVOR_B;
    return (int)identity(value) + 1;
}
