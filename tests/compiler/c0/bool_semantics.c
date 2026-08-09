typedef _Bool bool_alias;

unsigned long bool_size(void) {
    return sizeof(_Bool);
}

_Bool bool_return(int value) {
    return value;
}

int bool_assignment(int value) {
    _Bool flag;
    flag = value;
    return flag;
}

int bool_promotion(_Bool flag) {
    return flag + 2;
}

int bool_alias_roundtrip(bool_alias flag) {
    return flag;
}
