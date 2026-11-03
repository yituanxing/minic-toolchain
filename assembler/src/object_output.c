#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "minias_object_model.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

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

#ifdef _WIN32
/* Win32 treats these last path components as devices even with a suffix
 * (for example NUL.o). They have no regular-file attributes, but -o NUL
 * must still be a stream and must never enter the temporary-rename path. */
static bool windows_device_path(const char *path) {
    const char *component=path,*cursor;
    char stem[8];
    size_t count=0U;
    for(cursor=path;*cursor!='\0';++cursor)
        if(*cursor=='/' || *cursor=='\\' || *cursor==':')component=cursor+1;
    while(component[count]!='\0' && component[count]!='.' &&
          count<sizeof(stem)-1U) {
        unsigned char ch=(unsigned char)component[count];
        if(ch>='a' && ch<='z')ch=(unsigned char)(ch-'a'+'A');
        stem[count]=(char)ch;
        ++count;
    }
    while(count>0U && stem[count-1U]==' ')--count;
    stem[count]='\0';
    if(strcmp(stem,"CON")==0 || strcmp(stem,"PRN")==0 ||
       strcmp(stem,"AUX")==0 || strcmp(stem,"NUL")==0 ||
       strcmp(stem,"CONIN$")==0 || strcmp(stem,"CONOUT$")==0)
        return true;
    return (count==4U && stem[0]=='C' && stem[1]=='O' &&
            stem[2]=='M' && stem[3]>='1' && stem[3]<='9') ||
           (count==4U && stem[0]=='L' && stem[1]=='P' &&
            stem[2]=='T' && stem[3]>='1' && stem[3]<='9');
}
#endif

/* A complete object image reaches disk in a single same-directory rename.
 * The O_EXCL temporary never replaces a concurrent writer's temporary.
 * This publication contract is independent of ELF/COFF/Mach-O headers. */
bool minias_object_publish_image(const char *path,const unsigned char *image,
                                 size_t size,char *error,size_t capacity) {
    static const unsigned attempt_limit=128U;
#ifndef _WIN32
    struct stat old_stat;
#endif
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
#ifdef _WIN32
    /* GetFileAttributes does not follow a reparse point: a symlink remains
     * a stream destination, just as lstat does on POSIX. Inaccessible paths
     * are errors, not permission to overwrite an unknown destination. */
    if(windows_device_path(path) ||
       (path[0]=='\\' && path[1]=='\\' && path[2]=='.' &&
        path[3]=='\\')) {
        free(temporary);
        return write_stream(path,image,size,error,capacity);
    }
    {
        DWORD attributes=GetFileAttributesA(path);
        if(attributes!=INVALID_FILE_ATTRIBUTES) {
            if((attributes & (FILE_ATTRIBUTE_DIRECTORY |
                              FILE_ATTRIBUTE_REPARSE_POINT))!=0U) {
                free(temporary);
                return write_stream(path,image,size,error,capacity);
            }
            preserve_mode=true;
        } else {
            DWORD code=GetLastError();
            if(code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND) {
                output_error(error,capacity,"output-stat:%s",path);
                goto done;
            }
        }
    }
#else
    if(lstat(path,&old_stat)==0) {
        if(!S_ISREG(old_stat.st_mode)) {
            free(temporary);
            return write_stream(path,image,size,error,capacity);
        }
        preserve_mode=true;
    }
#endif
    for(attempt=0U;attempt<attempt_limit;++attempt) {
#ifdef _WIN32
        int length=snprintf(temporary,buffer_capacity,"%s.minias-tmp-%lu-%u",
                            path,(unsigned long)GetCurrentProcessId(),attempt);
#else
        int length=snprintf(temporary,buffer_capacity,"%s.minias-tmp-%ld-%u",
                            path,(long)getpid(),attempt);
#endif
        if(length<0 || (size_t)length>=buffer_capacity)break;
#ifdef _WIN32
        fd=_open(temporary,_O_WRONLY|_O_BINARY|_O_CREAT|_O_EXCL,
                 _S_IREAD|_S_IWRITE);
#else
        fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0666);
#endif
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
#ifndef _WIN32
    if(preserve_mode && fchmod(fd,old_stat.st_mode & 0777U)!=0) {
        output_error(error,capacity,"output-mode:%s",path);
        goto done;
    }
#endif
#ifdef _WIN32
    file=_fdopen(fd,"wb");
#else
    file=fdopen(fd,"wb");
#endif
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
#ifdef _WIN32
    /* ReplaceFile preserves the previous regular file's security descriptor
     * and attributes, rather than silently dropping its ACL on replacement.
     * Never fall back to a truncate/write or remove-then-rename sequence.
     * For new files MoveFileEx does not set REPLACE_EXISTING: if another
     * creator wins the race, their output is left intact. */
    if(!(preserve_mode
          ? ReplaceFileA(path,temporary,NULL,0U,NULL,NULL)
          : MoveFileExA(temporary,path,MOVEFILE_WRITE_THROUGH))) {
#else
    if(rename(temporary,path)!=0) {
#endif
        output_error(error,capacity,"output-rename:%s",path);
        goto done;
    }
    ok=true;
done:
    if(file!=NULL)(void)fclose(file);
#ifdef _WIN32
    if(fd>=0)(void)_close(fd);
    if(!ok && temporary_created)(void)DeleteFileA(temporary);
#else
    if(fd>=0)(void)close(fd);
    if(!ok && temporary_created)(void)unlink(temporary);
#endif
    free(temporary);
    return ok;
}
