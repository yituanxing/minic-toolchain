static double return_integer_as_double(int value) {
    return value;
}

static int matches_bytes(
    unsigned char *bytes, int byte2, int byte3, int byte4, int byte5, int byte6, int byte7) {
    if ((bytes[0] != 0) || (bytes[1] != 0)) {
        return 0;
    }
    return (bytes[2] == byte2) && (bytes[3] == byte3) && (bytes[4] == byte4) &&
           (bytes[5] == byte5) && (bytes[6] == byte6) && (bytes[7] == byte7);
}

int main(void) {
    int signed_word = -3;
    unsigned int unsigned_word = 0;
    long signed_long = -5;
    unsigned long unsigned_long = 0;
    double word_value = signed_word;
    double unsigned_word_value;
    double long_value = (double)signed_long;
    double unsigned_long_value;
    double returned_value;
    unsigned char *bytes = (unsigned char *)&word_value;

    if (!matches_bytes(bytes, 0, 0, 0, 0, 8, 192)) {
        return 1;
    }

    unsigned_word = unsigned_word - 1;
    unsigned_word_value = unsigned_word;
    bytes = (unsigned char *)&unsigned_word_value;
    if (!matches_bytes(bytes, 224, 255, 255, 255, 239, 65)) {
        return 2;
    }

    bytes = (unsigned char *)&long_value;
    if (!matches_bytes(bytes, 0, 0, 0, 0, 20, 192)) {
        return 3;
    }

    unsigned_long = unsigned_long - 1;
    unsigned_long_value = unsigned_long;
    bytes = (unsigned char *)&unsigned_long_value;
    if (!matches_bytes(bytes, 0, 0, 0, 0, 240, 67)) {
        return 4;
    }

    returned_value = return_integer_as_double(7);
    bytes = (unsigned char *)&returned_value;
    if (!matches_bytes(bytes, 0, 0, 0, 0, 28, 64)) {
        return 5;
    }
    return 0;
}
