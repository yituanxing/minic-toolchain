char global_padded[10] = "ratelimit";
static char global_exact[3] = "abc";

static int runtime_string_check(void) {
    char path[16] = "//enomem";
    char inferred[] = "x" "\n";

    return path[0] == '/' && path[1] == '/' && path[2] == 'e' && path[7] == 'm' &&
           path[8] == 0 && path[15] == 0 && sizeof(inferred) == 3 && inferred[0] == 'x' &&
           inferred[1] == '\n' && inferred[2] == 0;
}

int main(void) {
    return global_padded[0] == 'r' && global_padded[8] == 't' && global_padded[9] == 0 &&
                   global_exact[0] == 'a' && global_exact[2] == 'c' && runtime_string_check()
               ? 0
               : 1;
}
