static double literal_value(void) {
    return 123.5;
}

static int matches_double_bytes(unsigned char *bytes, int byte5, int byte6, int byte7) {
    if ((bytes[0] != 0) || (bytes[1] != 0) || (bytes[2] != 0) || (bytes[3] != 0) ||
        (bytes[4] != 0)) {
        return 0;
    }
    return (bytes[5] == byte5) && (bytes[6] == byte6) && (bytes[7] == byte7);
}

int main(void) {
    double value = 1.5;
    double copy = value;
    double from_call = literal_value();
    unsigned char *bytes = (unsigned char *)&copy;

    if (!matches_double_bytes(bytes, 0, 248, 63)) {
        return 1;
    }

    value = 1.5 + 2.25;
    bytes = (unsigned char *)&value;
    if (!matches_double_bytes(bytes, 0, 14, 64)) {
        return 2;
    }

    bytes = (unsigned char *)&from_call;
    if (!matches_double_bytes(bytes, 224, 94, 64)) {
        return 3;
    }
    return 0;
}
