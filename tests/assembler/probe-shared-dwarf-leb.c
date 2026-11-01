#include "minias_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_unsigned(uint64_t value,const unsigned char *expected,
                           size_t expected_length) {
    unsigned char actual[10];
    size_t length=0U;
    return minias_encode_uleb_integer(value,actual,&length) &&
           length==expected_length &&
           memcmp(actual,expected,length)==0;
}

static int expect_signed(int64_t value,const unsigned char *expected,
                         size_t expected_length) {
    unsigned char actual[10];
    size_t length=0U;
    return minias_encode_leb_integer(value,true,actual,&length) &&
           length==expected_length &&
           memcmp(actual,expected,length)==0;
}

int main(void) {
    static const unsigned char zero[]={0x00};
    static const unsigned char u128[]={0x80,0x01};
    static const unsigned char high[]={0x80,0x80,0x80,0x80,0x80,
                                       0x80,0x80,0x80,0x80,0x01};
    static const unsigned char max[]={0xff,0xff,0xff,0xff,0xff,
                                      0xff,0xff,0xff,0xff,0x01};
    static const unsigned char minus_one[]={0x7f};
    static const unsigned char signed_127[]={0xff,0x00};
    static const unsigned char min[]={0x80,0x80,0x80,0x80,0x80,
                                      0x80,0x80,0x80,0x80,0x7f};
    unsigned char rejected[10];
    size_t length=0U;
    if(!expect_unsigned(0U,zero,sizeof(zero)) ||
       !expect_unsigned(128U,u128,sizeof(u128)) ||
       !expect_unsigned(UINT64_C(0x8000000000000000),high,sizeof(high)) ||
       !expect_unsigned(UINT64_MAX,max,sizeof(max)) ||
       !expect_signed(-1,minus_one,sizeof(minus_one)) ||
       !expect_signed(127,signed_127,sizeof(signed_127)) ||
       !expect_signed(INT64_MIN,min,sizeof(min)) ||
       minias_encode_leb_integer(-1,false,rejected,&length)) {
        fputs("MINIAS_DWARF_LEB_SHARED=FAIL\n",stderr);
        return 1;
    }
    puts("MINIAS_DWARF_LEB_SHARED=PASS unsigned64=full signed64=full negative_unsigned=rejected");
    return 0;
}
