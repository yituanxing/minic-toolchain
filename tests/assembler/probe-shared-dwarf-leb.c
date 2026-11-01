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

static int expect_checked_le32_patch(void) {
    unsigned char bytes[8]={0xaaU,0xbbU,0U,0U,0U,0U,0xccU,0xddU};
    static const unsigned char middle[8]={0xaaU,0xbbU,0x44U,0x33U,
                                          0x22U,0x11U,0xccU,0xddU};
    static const unsigned char tail[8]={0xaaU,0xbbU,0x44U,0x33U,
                                        0xddU,0xccU,0xbbU,0xaaU};
    MiniAsSection section={0};
    MiniAs as={0};
    section.data=bytes;
    section.size=sizeof(bytes);
    as.sections=&section;
    as.section_count=1U;
    if(!minias_section_patch_le32(&as,0,2U,UINT32_C(0x11223344)) ||
       memcmp(bytes,middle,sizeof(bytes))!=0 ||
       minias_section_patch_le32(&as,0,5U,0U) ||
       minias_section_patch_le32(&as,0,SIZE_MAX,0U) ||
       minias_section_patch_le32(&as,0,2U,UINT64_C(0x100000000)) ||
       minias_section_patch_le32(&as,-1,2U,0U) ||
       minias_section_patch_le32(&as,1,2U,0U) ||
       minias_section_patch_le32(NULL,0,2U,0U) ||
       memcmp(bytes,middle,sizeof(bytes))!=0 ||
       !minias_section_patch_le32(&as,0,4U,UINT32_C(0xaabbccdd)) ||
       memcmp(bytes,tail,sizeof(bytes))!=0)return 0;
    section.data=NULL;
    return !minias_section_patch_le32(&as,0,0U,0U);
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
       minias_encode_leb_integer(-1,false,rejected,&length) ||
       !expect_checked_le32_patch()) {
        fputs("MINIAS_DWARF_LEB_SHARED=FAIL\n",stderr);
        return 1;
    }
    puts("MINIAS_DWARF_LEB_SHARED=PASS unsigned64=full signed64=full negative_unsigned=rejected");
    puts("MINIAS_DWARF_LE32_PATCH=PASS middle=1 tail=1 out_of_bounds=2 overflow=1 unchanged_on_error=1");
    return 0;
}
