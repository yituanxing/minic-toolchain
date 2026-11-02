#define _POSIX_C_SOURCE 200809L
#include "minias_object_model.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void output_error(char *error,size_t capacity,const char *format,...) {
    va_list ap;
    if(error==NULL || capacity==0U || error[0]!='\0')return;
    va_start(ap,format);
    (void)vsnprintf(error,capacity,format,ap);
    va_end(ap);
}

/* Pipes, devices (including -o /dev/null), directories and symlinks cannot
 * be replaced with a regular output file. Preserve stream semantics. */
static bool write_stream(const char *path,const unsigned char *image,
                         size_t size,char *error,size_t capacity) {
    FILE *file=fopen(path,"wb");
    size_t written;
    int close_status;
    if(file==NULL) {
        output_error(error,capacity,"output-open:%s",path);
        return false;
    }
    written=fwrite(image,1U,size,file);
    close_status=fclose(file);
    if(written!=size || close_status!=0) {
        output_error(error,capacity,"output-write:%s",path);
        return false;
    }
    return true;
}

/* A complete object image reaches disk in a single same-directory rename.
 * The O_EXCL temporary never replaces a concurrent writer's temporary.
 * This publication contract is independent of ELF/COFF/Mach-O headers. */
bool minias_object_publish_image(const char *path,const unsigned char *image,
                                 size_t size,char *error,size_t capacity) {
    static const unsigned attempt_limit=128U;
    struct stat old_stat;
    bool preserve_mode=false;
    size_t path_length,buffer_capacity;
    char *temporary;
    FILE *file=NULL;
    int fd=-1;
    unsigned attempt;
    bool temporary_created=false;
    bool ok=false;

    if(path==NULL || (image==NULL && size!=0U)) {
        output_error(error,capacity,"invalid-object-output");
        return false;
    }
    path_length=strlen(path);
    if(path_length>SIZE_MAX-48U) {
        output_error(error,capacity,"output-path-too-long");
        return false;
    }
    buffer_capacity=path_length+48U;
    temporary=malloc(buffer_capacity);
    if(temporary==NULL) {
        output_error(error,capacity,"output-temp-allocation");
        return false;
    }
    if(lstat(path,&old_stat)==0) {
        if(!S_ISREG(old_stat.st_mode)) {
            free(temporary);
            return write_stream(path,image,size,error,capacity);
        }
        preserve_mode=true;
    }
    for(attempt=0U;attempt<attempt_limit;++attempt) {
        int length=snprintf(temporary,buffer_capacity,"%s.minias-tmp-%ld-%u",
                            path,(long)getpid(),attempt);
        if(length<0 || (size_t)length>=buffer_capacity)break;
        fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0666);
        if(fd>=0) {
            temporary_created=true;
            break;
        }
        if(errno!=EEXIST)break;
    }
    if(fd<0) {
        output_error(error,capacity,"output-open:%s",path);
        goto done;
    }
    if(preserve_mode && fchmod(fd,old_stat.st_mode & 0777U)!=0) {
        output_error(error,capacity,"output-mode:%s",path);
        goto done;
    }
    file=fdopen(fd,"wb");
    if(file==NULL) {
        output_error(error,capacity,"output-open:%s",path);
        goto done;
    }
    fd=-1;
    {
        size_t written=fwrite(image,1U,size,file);
        int close_status=fclose(file);
        file=NULL;
        if(written!=size || close_status!=0) {
            output_error(error,capacity,"output-write:%s",path);
            goto done;
        }
    }
    if(rename(temporary,path)!=0) {
        output_error(error,capacity,"output-rename:%s",path);
        goto done;
    }
    ok=true;
done:
    if(file!=NULL)(void)fclose(file);
    if(fd>=0)(void)close(fd);
    if(!ok && temporary_created)(void)unlink(temporary);
    free(temporary);
    return ok;
}
