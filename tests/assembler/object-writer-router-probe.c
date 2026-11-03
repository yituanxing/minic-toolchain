#include "minias_object_writer.h"
#include "minias_elf_object_view.h"
#include "minias_coff_object_view.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned elf_calls;
static unsigned coff_calls;
static unsigned feature_calls;
static bool writer_failure;
static const MiniAsSection *expected_sections;
static const MiniAsElfTargetPolicy *expected_policy;

static bool test_coff_map(MiniAsTargetABIView abi,MiniAsFixupKind kind,
                          unsigned width,uint16_t *relocation) {
    assert(abi.address_size==8U && kind==MINIAS_FIXUP_ABS && width==8U);
    *relocation=1U;
    return true;
}

bool minias_write_coff_object(MiniAsCoffObjectView *view,const char *path) {
    ++coff_calls;
    assert(strcmp(path,"fake-output.o")==0);
    assert(view->model.sections==expected_sections);
    assert(view->model.section_count==1U);
    assert(view->machine==0x8664U);
    assert(view->target_abi.address_size==8U);
    assert(view->map_fixup==test_coff_map);
    if(writer_failure) {
        (void)snprintf(view->error,sizeof(view->error),
                       "mock-coff-serializer-failure");
        return false;
    }
    return true;
}

static uint32_t project_features(MiniAsTargetABIView abi,uint32_t features) {
    ++feature_calls;
    assert(abi.address_size==4U);
    assert(features==0x05U);
    return 0x40U;
}

/* The format dispatcher is tested independently of the real ELF serializer;
 * the existing ELF writer oracle checks genuine section/relocation bytes. */
bool minias_write_elf_object(MiniAsElfObjectView *view,const char *path) {
    ++elf_calls;
    assert(strcmp(path,"fake-output.o")==0);
    assert(view->model.sections==expected_sections);
    assert(view->model.section_count==1U);
    assert(view->policy==expected_policy);
    assert(view->target_abi.address_size==4U);
    assert(view->object_machine==243U);
    assert(view->output_rel_relocations);
    assert(view->suppress_target_attributes);
    assert(view->elf_flags==0x42U);
    if(writer_failure) {
        (void)snprintf(view->error,sizeof(view->error),
                       "mock-elf-serializer-failure");
        return false;
    }
    return true;
}

int main(void) {
    MiniAsSection section={0};
    MiniAsObjectSpec spec={0};
    MiniAsElfTargetPolicy policy={0};
    MiniAsCoffTargetPolicy coff_policy={0};
    MiniAsObjectWriterRequest request={0};

    expected_sections=&section;
    expected_policy=&policy;
    spec.format=MINIAS_OBJECT_FORMAT_ELF;
    spec.elf.initial_flags=0x02U;
    spec.elf.machine=243U;
    spec.elf.rel_relocations=true;
    spec.elf.suppress_target_attributes=true;
    policy.project_output_features=project_features;
    request.model.sections=&section;
    request.model.section_count=1U;
    request.spec=&spec;
    request.target_abi.address_size=4U;
    request.target_output_features=0x05U;
    request.elf.policy=&policy;

    assert(minias_write_object(&request,"fake-output.o"));
    assert(elf_calls==1U && feature_calls==1U);
    assert(request.error[0]=='\0');

    /* A second real format receives the identical borrowed ObjectModel,
     * not an ELF object masquerading as COFF or a second mutable Context. */
    coff_policy.machine=0x8664U;
    coff_policy.map_fixup=test_coff_map;
    request.coff.policy=&coff_policy;
    request.target_abi.address_size=8U;
    spec.format=MINIAS_OBJECT_FORMAT_COFF;
    assert(minias_write_object(&request,"fake-output.o"));
    assert(coff_calls==1U && elf_calls==1U && feature_calls==1U);
    request.coff.policy=NULL;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"unsupported-coff-target-policy")==0);
    request.coff.policy=&coff_policy;
    writer_failure=true;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"mock-coff-serializer-failure")==0);
    assert(coff_calls==2U && elf_calls==1U);
    writer_failure=false;
    request.target_abi.address_size=4U;

    /* Unsupported formats cannot silently produce ELF; no callback, file
     * creation or second semantic model exists on this path. */
    spec.format=(MiniAsObjectFormat)99;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"unsupported-object-format")==0);
    assert(elf_calls==1U && feature_calls==1U);

    spec.format=MINIAS_OBJECT_FORMAT_ELF;
    writer_failure=true;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"mock-elf-serializer-failure")==0);
    assert(elf_calls==2U && feature_calls==2U);

    request.elf.policy=NULL;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"missing-elf-target-output-policy")==0);
    assert(elf_calls==2U && feature_calls==2U);

    request.elf.policy=&policy;
    policy.project_output_features=NULL;
    assert(!minias_write_object(&request,"fake-output.o"));
    assert(strcmp(request.error,"missing-elf-target-output-policy")==0);
    assert(elf_calls==2U && feature_calls==2U);

    puts("MINIAS_OBJECT_WRITER_ROUTER=PASS ELF_projection=1 "
         "COFF_native_projection=1 unknown_format=reject missing_policy=reject "
         "writer_error_propagation=2 semantic_model=borrowed");
    return 0;
}
