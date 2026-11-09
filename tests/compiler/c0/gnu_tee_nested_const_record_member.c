typedef struct KUid { unsigned int val; } KUid;
typedef struct Cred { KUid euid; } Cred;
static Cred credential = { { 77U } };
static volatile Cred volatile_credential = { { 145U } };
static unsigned int credential_calls;
static unsigned int volatile_credential_calls;

static const Cred *current_cred(void) { credential_calls++; return &credential; }
static const volatile Cred *current_cred_volatile(void) {
    volatile_credential_calls++;
    return &volatile_credential;
}
extern int snprintf(char *buffer, unsigned long size, const char *format, ...);

int current_uid(char *buffer) {
    return snprintf(buffer, 128UL, "uid=%x",
        (({ ({ do { } while (0); ;
               ((typeof(*(current_cred())) *)(current_cred()));
            })->euid; })).val);
}

int current_uid_volatile(char *buffer) {
    return snprintf(buffer, 128UL, "uid=%x",
        (({ ({ do { } while (0); ;
               ((typeof(*(current_cred_volatile())) *)(current_cred_volatile()));
            })->euid; })).val);
}

int main(void) {
    char first[128], second[128];
    return current_uid(first) == 6 && first[0] == 'u' &&
        first[4] == '4' && first[5] == 'd' && first[6] == 0 &&
        current_uid_volatile(second) == 6 && second[0] == 'u' &&
        second[4] == '9' && second[5] == '1' && second[6] == 0 &&
        credential_calls == 1 && volatile_credential_calls == 1 ? 0 : 1;
}
