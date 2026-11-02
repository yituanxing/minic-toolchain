#include "minias_elf_object_view.h"

#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Link the production ObjectModel, ELF writer and minielf serializer as
 * standalone translation units. The valid probe has no symbols or fixups,
 * so neither of these separate Common services may be invoked. */
bool minias_project_symbol(const MiniAsSymbol *symbol,MiniAsSymbolView *out) {
    (void)symbol;
    (void)out;
    abort();
}

bool minias_is_private_symbol_name(const char *name) {
    assert(strcmp(name,"semantic_symbol")==0);
    return false;
}

char *minias_strdup(const char *source) {
    size_t length=strlen(source)+1U;
    char *copy=malloc(length);
    if(copy!=NULL)memcpy(copy,source,length);
    return copy;
}

static bool find_symbol_id(const void *reader,const char *name,size_t *id) {
    (void)reader;
    (void)name;
    (void)id;
    return false;
}

static int find_section(const void *reader,const char *name) {
    (void)reader;
    (void)name;
    return -1;
}

static bool numeric_reference(const char *name) {
    (void)name;
    return false;
}

static bool fragment_location(const void *reader,size_t id,
                              MiniAsObjectFragmentLocation *location) {
    (void)reader;
    (void)id;
    (void)location;
    return false;
}

static bool section_bytes(const void *reader,int section,
                          const unsigned char **data,size_t *size) {
    static const unsigned char payload[4]={0x11U,0x22U,0x33U,0x44U};
    (void)reader;
    if(section!=0)return false;
    *data=payload;
    *size=sizeof(payload);
    return true;
}

static bool write_section(MiniAsElfObjectView *view,const char *path) {
    view->error[0]='\0';
    return minias_write_elf64(view,path);
}

int main(int argc,char **argv) {
    MiniAsSection section={.name=".semantic.probe",.align=1U,
                          .type=MINIAS_SECTION_KIND_PROGBITS};
    MiniAsSymbol symbol={.name="semantic_symbol",.defined=true,.section=0,
                         .bind=MINIAS_SYMBOL_BINDING_GLOBAL,
                         .type=MINIAS_SYMBOL_KIND_OBJECT,
                         .visibility=MINIAS_SYMBOL_VISIBILITY_DEFAULT};
    MiniAsElfObjectView view={0};
    static const MiniAsSectionKind kinds[]={
        MINIAS_SECTION_KIND_PROGBITS,MINIAS_SECTION_KIND_NOBITS,
        MINIAS_SECTION_KIND_NOTE,MINIAS_SECTION_KIND_INIT_ARRAY,
        MINIAS_SECTION_KIND_FINI_ARRAY,MINIAS_SECTION_KIND_PREINIT_ARRAY
    };
    char filename[512];
    size_t i;
    if(argc!=2)return 64;
    view.model.sections=&section;
    view.model.section_count=1U;
    view.model.find_symbol_id=find_symbol_id;
    view.model.find_section=find_section;
    view.model.numeric_reference=numeric_reference;
    view.model.fragment_location=fragment_location;
    view.model.section_bytes=section_bytes;
    view.object_machine=EM_X86_64;

    for(i=0U;i<sizeof(kinds)/sizeof(kinds[0]);++i) {
        int length=snprintf(filename,sizeof(filename),"%s-%zu.o",argv[1],i);
        if(length<0 || (size_t)length>=sizeof(filename))return 64;
        section.type=kinds[i];
        if(!write_section(&view,filename) || view.error[0]!='\0') {
            fprintf(stderr,"valid-section-kind=%zu failed: %s\n",i,view.error);
            return 1;
        }
    }

    section.type=(MiniAsSectionKind)255;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("unknown semantic Section kind escaped ELF projection\n",stderr);
        return 1;
    }
    section.type=MINIAS_SECTION_KIND_PROGBITS;
    section.flags=UINT64_C(1)<<63;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("unknown semantic Section flag escaped ELF projection\n",stderr);
        return 1;
    }
    section.flags=0U;
    section.group_comdat=true;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("COMDAT group with no signature escaped ELF projection\n",stderr);
        return 1;
    }
    section.group_comdat=false;
    section.flags=MINIAS_SECTION_FLAG_GROUP;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("GROUP attribute with no signature escaped ELF projection\n",stderr);
        return 1;
    }
    section.flags=0U;
    view.model.symbols=&symbol;
    view.model.symbol_count=1U;
    for(i=0U;i<4U;++i) {
        int length=snprintf(filename,sizeof(filename),"%s-symbol-%zu.o",argv[1],i);
        if(length<0 || (size_t)length>=sizeof(filename))return 64;
        symbol.bind=i==0U?MINIAS_SYMBOL_BINDING_GNU_UNIQUE:
                    MINIAS_SYMBOL_BINDING_GLOBAL;
        symbol.type=i==1U?MINIAS_SYMBOL_KIND_GNU_IFUNC:
                    i==2U?MINIAS_SYMBOL_KIND_TLS:MINIAS_SYMBOL_KIND_OBJECT;
        symbol.visibility=i==3U?MINIAS_SYMBOL_VISIBILITY_HIDDEN:
                          MINIAS_SYMBOL_VISIBILITY_DEFAULT;
        if(!write_section(&view,filename) || view.error[0]!='\0') {
            fprintf(stderr,"valid-symbol-semantics=%zu failed: %s\n",i,view.error);
            return 1;
        }
    }
    symbol.bind=(MiniAsSymbolBinding)255;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("unknown Symbol binding escaped ELF projection\n",stderr);
        return 1;
    }
    symbol.bind=MINIAS_SYMBOL_BINDING_GLOBAL;
    symbol.type=(MiniAsSymbolKind)255;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("unknown Symbol kind escaped ELF projection\n",stderr);
        return 1;
    }
    symbol.type=MINIAS_SYMBOL_KIND_OBJECT;
    symbol.visibility=(MiniAsSymbolVisibility)255;
    if(write_section(&view,"/dev/null") || view.error[0]=='\0') {
        fputs("unknown Symbol visibility escaped ELF projection\n",stderr);
        return 1;
    }
    puts("MINIAS_SECTION_ELF_PROJECTION=PASS kinds=6 invalid_kind=reject invalid_flag=reject invalid_group=2");
    puts("MINIAS_SYMBOL_ELF_PROJECTION=PASS semantics=4 invalid_binding_kind_visibility=reject");
    return 0;
}
