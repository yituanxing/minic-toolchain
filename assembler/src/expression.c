#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool is_symbol_start(char ch) {
    return ch == '.' || ch == '$' || ch == '_' || isalpha((unsigned char)ch);
}

static bool is_symbol_continue(char ch) {
    return is_symbol_start(ch) || isdigit((unsigned char)ch);
}

bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr) {
    const char *p;
    const char *start;
    size_t length;
    int sign = 1;
    char *end = NULL;
    long long addend = 0;

    if (text == NULL || expr == NULL) {
        return false;
    }
    p = text;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (!is_symbol_start(*p)) {
        return false;
    }
    start = p++;
    while (is_symbol_continue(*p)) {
        ++p;
    }
    length = (size_t)(p - start);
    if (length == 0U || length >= sizeof(expr->name)) {
        return false;
    }
    memcpy(expr->name, start, length);
    expr->name[length] = '\0';

    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '+' || *p == '-') {
        if (*p == '-') {
            sign = -1;
        }
        ++p;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        errno = 0;
        addend = strtoll(p, &end, 0);
        if (errno != 0 || end == p) {
            return false;
        }
        p = end;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
    }
    if (*p != '\0') {
        return false;
    }
    expr->addend = (int64_t)addend * sign;
    return true;
}
