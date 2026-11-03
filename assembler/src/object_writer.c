#include "minias_object_writer.h"
#include "minias_elf_object_view.h"

#include <stdio.h>

/* Keep the format switch at one boundary. The only implemented format is
 * ELF; a future writer consumes the same ObjectModel, not a second graph or
 * any private Common/Target mutable record. */
bool minias_write_object(MiniAsObjectWriterRequest *request,const char *path) {
    MiniAsElfObjectView elf={0};
    const MiniAsElfTargetPolicy *policy;
    if(request==NULL)return false;
    request->error[0]='\0';
    if(request->spec==NULL || path==NULL) {
        (void)snprintf(request->error,sizeof(request->error),
                       "invalid-object-writer-request");
        return false;
    }
    if(request->spec->format!=MINIAS_OBJECT_FORMAT_ELF) {
        (void)snprintf(request->error,sizeof(request->error),
                       "unsupported-object-format");
        return false;
    }
    policy=request->elf.policy;
    if(policy==NULL || (request->target_output_features!=0U &&
                       policy->project_output_features==NULL)) {
        (void)snprintf(request->error,sizeof(request->error),
                       "missing-elf-target-output-policy");
        return false;
    }
    elf.model=request->model;
    elf.policy=policy;
    elf.target_abi=request->target_abi;
    elf.object_machine=request->spec->elf.machine;
    elf.output_rel_relocations=request->spec->elf.rel_relocations;
    elf.suppress_target_attributes=request->spec->elf.suppress_target_attributes;
    elf.elf_flags=request->spec->elf.initial_flags;
    if(policy->project_output_features!=NULL)
        elf.elf_flags|=policy->project_output_features(request->target_abi,
                                                      request->target_output_features);
    if(!minias_write_elf_object(&elf,path)) {
        (void)snprintf(request->error,sizeof(request->error),"%s",
                       elf.error[0]!='\0'?elf.error:"elf-object-write-failed");
        return false;
    }
    return true;
}
