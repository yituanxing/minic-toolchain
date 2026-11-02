#include "minias_object_model.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniAsObjectSectionName {
    const char *name;
    size_t section_id;
} MiniAsObjectSectionName;

static void object_error(char *error,size_t capacity,const char *format,...) {
    va_list ap;
    if(error==NULL || capacity==0U || error[0]!='\0')return;
    va_start(ap,format);
    (void)vsnprintf(error,capacity,format,ap);
    va_end(ap);
}

static int compare_section_name(const void *lhs,const void *rhs) {
    const MiniAsObjectSectionName *a=lhs,*b=rhs;
    int cmp=strcmp(a->name,b->name);
    if(cmp!=0)return cmp;
    return a->section_id<b->section_id?-1:
           a->section_id>b->section_id?1:0;
}

static size_t first_named_section(const MiniAsObjectSectionName *index,
                                  size_t count,const char *name) {
    size_t lo=0U,hi=count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2U;
        if(strcmp(index[mid].name,name)<0)lo=mid+1U;
        else hi=mid;
    }
    return lo;
}

/* Resolve symbolic section associations while SectionID still has its
 * original identity. The eventual ELF sh_link, COFF COMDAT selection or
 * Mach-O association is *not* computed here. */
static bool resolve_link_order(const MiniAsObjectModelView *view,size_t owner,
                               const MiniAsObjectSectionName *names,
                               size_t name_count,size_t *target,
                               char *error,size_t capacity) {
    const MiniAsSection *section=&view->sections[owner];
    const char *reference=section->link_order_reference;
    const MiniAsSymbol *symbol;
    size_t symbol_id;
    size_t i,candidate=SIZE_MAX;
    *target=SIZE_MAX;
    if(reference==NULL || *reference=='\0')return true;
    if(view->numeric_reference(reference))return true;
    if(view->find_symbol_id(view->reader,reference,&symbol_id)) {
        if(symbol_id>=view->symbol_count) {
            object_error(error,capacity,"invalid-link-order-symbol-id:%s",reference);
            return false;
        }
        symbol=&view->symbols[symbol_id];
    } else symbol=NULL;
    if(symbol!=NULL && symbol->defined) {
        if(symbol->section==MINIAS_SECTION_ABS)return true;
        if(symbol->section>=0 &&
           (size_t)symbol->section<view->section_count) {
            *target=(size_t)symbol->section;
            return true;
        }
        object_error(error,capacity,"invalid-link-order-target:%s:section=%s",
                     reference,section->name);
        return false;
    }
    for(i=first_named_section(names,name_count,reference);
        i<name_count && strcmp(names[i].name,reference)==0;++i) {
        const size_t section_id=names[i].section_id;
        const MiniAsSection *other=&view->sections[section_id];
        if(candidate==SIZE_MAX)candidate=section_id;
        else if(section->group_signature!=NULL &&
                other->group_signature!=NULL &&
                strcmp(section->group_signature,other->group_signature)==0) {
            candidate=section_id;
            break;
        } else if(section->group_signature==NULL) {
            object_error(error,capacity,
                         "ambiguous-link-order-section:%s:section=%s",
                         reference,section->name);
            return false;
        }
    }
    if(candidate!=SIZE_MAX) {
        *target=candidate;
        return true;
    }
    object_error(error,capacity,"undefined-link-order-reference:%s:section=%s",
                 reference,section->name);
    return false;
}

void minias_object_dispose_plan(MiniAsObjectPlan *plan) {
    if(plan==NULL)return;
    free(plan->sections);
    free(plan->fixups);
    *plan=(MiniAsObjectPlan){0};
}

bool minias_object_plan(const MiniAsObjectModelView *view,
                        MiniAsObjectPlan *plan,char *error,size_t capacity) {
    MiniAsObjectSectionName *names=NULL;
    size_t i;
    bool ok=false;
    if(view==NULL || plan==NULL ||
       (view->section_count!=0U && view->sections==NULL) ||
       (view->symbol_count!=0U && view->symbols==NULL) ||
       (view->fixup_count!=0U && view->fixups==NULL) ||
       view->find_symbol_id==NULL || view->numeric_reference==NULL ||
       view->section_bytes==NULL ||
       view->fragment_location==NULL) {
        object_error(error,capacity,"invalid-object-model-view");
        return false;
    }
    *plan=(MiniAsObjectPlan){0};
    if(view->section_count>(size_t)INT_MAX ||
       view->section_count>SIZE_MAX/sizeof(*plan->sections) ||
       view->section_count>SIZE_MAX/sizeof(*names) ||
       view->fixup_count>SIZE_MAX/sizeof(*plan->fixups)) {
        object_error(error,capacity,"elf-object-capacity-overflow");
        return false;
    }
    plan->sections=calloc(view->section_count==0U?1U:view->section_count,
                          sizeof(*plan->sections));
    plan->fixups=calloc(view->fixup_count==0U?1U:view->fixup_count,
                        sizeof(*plan->fixups));
    names=malloc((view->section_count==0U?1U:view->section_count)*
                 sizeof(*names));
    if(plan->sections==NULL || plan->fixups==NULL || names==NULL) {
        object_error(error,capacity,"out-of-memory:object-reference-plan");
        goto done;
    }
    for(i=0U;i<view->section_count;++i) {
        const MiniAsSection *section=&view->sections[i];
        MiniAsObjectSectionPlan *output=&plan->sections[i];
        size_t signature_id;
        if(section->name==NULL) {
            object_error(error,capacity,"unnamed-elf-section");
            goto done;
        }
        names[i]=(MiniAsObjectSectionName){section->name,i};
        output->signature_symbol=SIZE_MAX;
        output->linked_section=SIZE_MAX;
        if(!view->section_bytes(view->reader,(int)i,
                                &output->bytes,&output->size)) {
            object_error(error,capacity,"invalid-elf-section-bytes");
            goto done;
        }
        if(section->group_signature==NULL)continue;
        if(!view->find_symbol_id(view->reader,section->group_signature,
                                 &signature_id) ||
           signature_id>=view->symbol_count) {
            object_error(error,capacity,"missing-group-signature:%s",
                         section->name);
            goto done;
        }
        output->signature_symbol=signature_id;
    }
    qsort(names,view->section_count,sizeof(*names),compare_section_name);
    for(i=0U;i<view->section_count;++i)
        if(view->sections[i].link_order_reference!=NULL &&
           !resolve_link_order(view,i,names,view->section_count,
                               &plan->sections[i].linked_section,
                               error,capacity))goto done;
    for(i=0U;i<view->fixup_count;++i) {
        const MiniAsFixup *fixup=&view->fixups[i];
        uint64_t offset;
        if(fixup->origin==MINIAS_FIXUP_ORIGIN_FRAGMENT) {
            MiniAsObjectFragmentLocation fragment;
            if(!view->fragment_location(view->reader,fixup->fragment_id,
                                        &fragment)) {
                object_error(error,capacity,"invalid-fixup-fragment-id");
                goto done;
            }
            if(fragment.section!=fixup->section ||
               fixup->fragment_offset>fragment.size ||
               fixup->width>fragment.size-fixup->fragment_offset ||
               fragment.offset>UINT64_MAX-fixup->fragment_offset) {
                object_error(error,capacity,
                             "invalid-fixup-fragment-layout:line=%zu",
                             fragment.line);
                goto done;
            }
            offset=fragment.offset+fixup->fragment_offset;
        } else if(fixup->origin==MINIAS_FIXUP_ORIGIN_EXPLICIT_SECTION ||
                  fixup->origin==MINIAS_FIXUP_ORIGIN_SYNTHETIC_SECTION) {
            offset=fixup->offset;
        } else {
            object_error(error,capacity,"invalid-fixup-origin");
            goto done;
        }
        if(fixup->section<0 || (size_t)fixup->section>=view->section_count ||
           (fixup->symbol_index!=SIZE_MAX &&
            fixup->symbol_index>=view->symbol_count)) {
            object_error(error,capacity,"internal:bad-relocation-symbol");
            goto done;
        }
        {
            const size_t size=plan->sections[(size_t)fixup->section].size;
            if(fixup->width>size || offset>size-fixup->width) {
                object_error(error,capacity,
                             "fixup-field-out-of-bounds:kind=%u",
                             (unsigned)fixup->kind);
                goto done;
            }
        }
        plan->fixups[i].final_offset=offset;
    }
    ok=true;
done:
    free(names);
    if(!ok)minias_object_dispose_plan(plan);
    return ok;
}
