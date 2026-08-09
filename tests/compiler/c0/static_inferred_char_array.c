static const char local_name[] = "lo" "cal";
static const char upvalue_name[] = "upvalue";

int main(void) {
    return sizeof(local_name) == 6 && sizeof(upvalue_name) == 8 && local_name[0] == 'l' &&
                   local_name[4] == 'l' && local_name[5] == 0 && upvalue_name[7] == 0
               ? 0
               : 1;
}
