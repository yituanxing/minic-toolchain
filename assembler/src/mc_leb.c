#include "minias_internal.h"

#include <stdint.h>

/* Shared full-width ULEB bytes: DWARF attributes may legitimately contain
 * values above INT64_MAX even though source .uleb128 expressions currently
 * use the checked signed expression evaluator. */
bool minias_encode_uleb_integer(uint64_t number,unsigned char output[10],
                                size_t *length) {
    size_t n=0U;
    do {
        uint8_t byte=(uint8_t)(number&0x7fU);
        number>>=7U;
        output[n++]=(unsigned char)(byte|(number?0x80U:0U));
    }while(number);
    *length=n;
    return true;
}

/* One encoding contract for eagerly resolved LEB literals, retained
 * deferred fragments and both DWARF writers. Never reparse source text. */
bool minias_encode_leb_integer(int64_t number,bool signed_leb,
                               unsigned char output[10],size_t *length) {
    size_t n=0U;
    if(!signed_leb)return number>=0 &&
        minias_encode_uleb_integer((uint64_t)number,output,length);
    for(;;) {
        uint8_t byte=(uint8_t)((uint64_t)number&0x7fU);
        bool done;
        number>>=7U;
        done=(number==0 && (byte&0x40U)==0U) ||
             (number==-1 && (byte&0x40U)!=0U);
        if(n>=10U)return false;
        output[n++]=(unsigned char)(byte | (done?0U:0x80U));
        if(done){*length=n;return true;}
    }
}
