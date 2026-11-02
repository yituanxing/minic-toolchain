#include "minias_internal.h"
#include "minias_common_emission.h"
#include "minias_common_context.h"

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
    MiniAsEmittedSectionView view;
    int ok;
    section.type=MINIAS_SECTION_KIND_PROGBITS;
    as.sections=&section;
    as.section_count=1U;
    if(!minias_common_emission_init(&as))return 0;
    if(!minias_section_append(&as,0,bytes,sizeof(bytes))) {
        minias_common_emission_destroy(&as);
        return 0;
    }
    ok=minias_section_patch_le32(&as,0,2U,UINT32_C(0x11223344)) &&
       minias_emission_section_view(&as,0,&view) &&
       memcmp(view.data,middle,sizeof(bytes))==0 &&
       !minias_section_patch_le32(&as,0,5U,0U) &&
       !minias_section_patch_le32(&as,0,SIZE_MAX,0U) &&
       !minias_section_patch_le32(&as,0,2U,UINT64_C(0x100000000)) &&
       !minias_section_patch_le32(&as,-1,2U,0U) &&
       !minias_section_patch_le32(&as,1,2U,0U) &&
       !minias_section_patch_le32(NULL,0,2U,0U) &&
       minias_emission_section_view(&as,0,&view) &&
       memcmp(view.data,middle,sizeof(bytes))==0 &&
       minias_section_patch_le32(&as,0,4U,UINT32_C(0xaabbccdd)) &&
       minias_emission_section_view(&as,0,&view) &&
       memcmp(view.data,tail,sizeof(bytes))==0;
    minias_common_emission_destroy(&as);
    return ok && !minias_section_patch_le32(&as,0,0U,0U);
}

static int expect_section_owner_split(void) {
    MiniAs as={0};
    MiniAsEmittedSectionView text={0},bss={0};
    const unsigned char payload[]={0x83U,0x01U,0x80U,0xffU};
    const unsigned char masked[]={0x83U,0x00U,0x80U,0xffU};
    int text_id,bss_id;
    int ok=0;
    if(!minias_common_context_init(&as))return 0;
    if(!minias_common_emission_init(&as))goto done;
    text_id=minias_ensure_section(&as,".text",MINIAS_SECTION_KIND_PROGBITS,
                  MINIAS_SECTION_FLAG_ALLOC|MINIAS_SECTION_FLAG_EXECINSTR,4U);
    bss_id=minias_ensure_section(&as,".bss",MINIAS_SECTION_KIND_NOBITS,
                    MINIAS_SECTION_FLAG_ALLOC|MINIAS_SECTION_FLAG_WRITE,1U);
    if(text_id!=0 || bss_id!=1 ||
       !minias_section_append(&as,text_id,payload,sizeof(payload)) ||
       !minias_section_append_zero(&as,bss_id,19U))goto done;
    /* Grow Context's metadata allocation after Emission already owns bytes:
     * SectionIDs remain stable, with no pointer pair to synchronize. */
    for(int i=0;i<80;++i) {
        char name[32];
        snprintf(name,sizeof(name),".probe.%d",i);
        if(minias_ensure_section(&as,name,MINIAS_SECTION_KIND_PROGBITS,0U,1U)<0)
            goto done;
    }
    if(as.section_count!=82U ||
       as.sections[0].type!=MINIAS_SECTION_KIND_PROGBITS ||
       as.sections[1].type!=MINIAS_SECTION_KIND_NOBITS ||
       !minias_emission_section_view(&as,text_id,&text) ||
       !minias_emission_section_view(&as,bss_id,&bss) ||
       text.size!=sizeof(payload) || bss.size!=19U || bss.data!=NULL ||
       memcmp(text.data,payload,sizeof(payload))!=0 ||
       !minias_emission_clear_leb_payload(&as,text_id,1U,2U) ||
       !minias_emission_section_view(&as,text_id,&text) ||
       memcmp(text.data,masked,sizeof(masked))!=0 ||
       minias_emission_clear_leb_payload(&as,text_id,3U,2U) ||
       minias_emission_clear_leb_payload(&as,bss_id,0U,1U) ||
       minias_emission_clear_leb_payload(&as,-1,0U,1U) ||
       !minias_emission_section_view(&as,text_id,&text) ||
       memcmp(text.data,masked,sizeof(masked))!=0)goto done;
    ok=1;
done:
    minias_common_emission_destroy(&as);
    minias_common_context_destroy_sections(&as);
    minias_common_context_destroy_symbols(&as);
    return ok;
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
       !expect_checked_le32_patch() || !expect_section_owner_split()) {
        fputs("MINIAS_DWARF_LEB_SHARED=FAIL\n",stderr);
        return 1;
    }
    puts("MINIAS_DWARF_LEB_SHARED=PASS unsigned64=full signed64=full negative_unsigned=rejected");
    puts("MINIAS_DWARF_LE32_PATCH=PASS middle=1 tail=1 out_of_bounds=2 overflow=1 unchanged_on_error=1");
    puts("MINIAS_SECTION_OWNER_SPLIT=PASS section_ids=82 metadata=Context bytes=Emission NOBITS=19 LEB_mask=Emission bounds=rejected");
    return 0;
}
