/* Focused user-space reproduction of the real Python 145/203 M38
 * "iptables-get-info" operation. This is NOT a replacement for the
 * untouched historical 145/203 release oracle.
 *
 * Build (GNU userland, separate from MiniC kernel): riscv64-linux-gnu-gcc
 * -O2 -static this_file.c -o init
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Order copied from the ORIGINAL passing Python V024 guest's MODULE_PASS
 * sequence, not invented. Extra network modules from indices 100..105 are
 * appended after the first 39. Choose prefix via /proc/cmdline:
 * netfilter_upto=3 or netfilter_upto=39 etc. */
static const char *module_names[] = {
  "pcs_xpcs", "nls_iso8859-1", "dummy", "efivarfs", "overlay",
  "x_tables", "ip_tables", "iptable_filter", "libcrc32c",
  "nf_defrag_ipv4", "nf_defrag_ipv6", "nf_conntrack",
  "llc", "stp", "bridge", "br_netfilter",
  "ip6_tables", "ip6table_filter", "nf_log_syslog",
  "veth", "macvlan", "iptable_mangle", "ip6table_mangle",
  "xt_mark", "xt_tcpudp", "ip6t_ipv6header",
  "nf_reject_ipv4", "ipt_REJECT", "nf_reject_ipv6", "ip6t_REJECT",
  "nf_nat", "nf_conntrack_ftp", "nf_conntrack_tftp", "nf_nat_ftp",
  "nf_nat_tftp", "iptable_nat", "xt_nat", "xt_REDIRECT", "xt_MASQUERADE",
  "ip_vs", "ip_vs_rr", "xt_addrtype", "xt_conntrack", "xt_ipvs", "xfrm_user"
};

static int resolve_path(const char *dir, const char *name, char *out, size_t capacity) {
  DIR *handle=opendir(dir);
  if (!handle) return -1;
  struct dirent *entry;
  int result=-1;
  while ((entry=readdir(handle))) {
    if (entry->d_name[0]=='.') continue;
    char path[1024];
    if (snprintf(path,sizeof(path),"%s/%s",dir,entry->d_name)>=(int)sizeof(path)) continue;
    struct stat st;
    if (lstat(path,&st)!=0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (resolve_path(path,name,out,capacity)==0) { result=0;break; }
    } else if (S_ISREG(st.st_mode)) {
      size_t len=strlen(entry->d_name), stem=strlen(name);
      if (len==stem+3 && memcmp(entry->d_name,name,stem)==0
          && strcmp(entry->d_name+stem,".ko")==0) {
        if (snprintf(out,capacity,"%s",path)<(int)capacity) result=0;
        break;
      }
    }
  }
  closedir(handle);
  return result;
}
static int prefix_from_cmdline(void) {
  FILE *f=fopen("/proc/cmdline","r");
  if (!f) return 8;
  char line[4096];
  if (!fgets(line,sizeof(line),f)) { fclose(f);return 8;}
  fclose(f);
  char *key=strstr(line,"netfilter_upto=");
  if (!key) return 8;
  int n=atoi(key+strlen("netfilter_upto="));
  if (n<1 || n>(int)(sizeof(module_names)/sizeof(module_names[0]))) return -1;
  return n;
}

static int insert_module(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) { printf("NETFILTER_PROBE=FAIL stage=open path=%s errno=%d\n",path,errno); return -1; }
  off_t length = lseek(fd, 0, SEEK_END);
  if (length <= 0 || lseek(fd, 0, SEEK_SET) != 0) {close(fd);return -1;}
  void *buffer = malloc((size_t)length);
  if (!buffer) {close(fd);return -1;}
  size_t at=0;
  while (at < (size_t)length) {
    ssize_t got = read(fd, (char *)buffer + at, (size_t)length-at);
    if (got <= 0) {printf("NETFILTER_PROBE=FAIL stage=read path=%s\n",path);free(buffer);close(fd);return -1;}
    at += (size_t)got;
  }
  close(fd);
  int result = (int)syscall(SYS_init_module, buffer, (unsigned long)length, "");
  int err = errno;
  free(buffer);
  if (result < 0 && err != EEXIST) {
    printf("NETFILTER_PROBE=FAIL stage=init_module path=%s errno=%d\n",path,err);
    return -1;
  }
  printf("NETFILTER_PROBE=MODULE_OK path=%s\n",path);
  fflush(stdout);
  return 0;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  (void)mount("proc","/proc","proc",0,NULL);
  int upto=prefix_from_cmdline();
  printf("NETFILTER_PROBE=BEGIN upto=%d\n",upto);
  if(upto<1) goto out;
  for(int i=0;i<upto;i++) {
    char path[1024];
    if(resolve_path("/lib/modules/6.6.143",module_names[i],path,sizeof(path))<0) {
      printf("NETFILTER_PROBE=FAIL stage=resolve module=%s\n",module_names[i]);
      goto out;
    }
    if (insert_module(path) != 0) goto out;
    printf("NETFILTER_PROBE=MODULE_STEP index=%d name=%s\n",i+1,module_names[i]);
  }
  printf("NETFILTER_PROBE=GETINFO_BEGIN\n");
  int fd=socket(AF_INET,SOCK_RAW,IPPROTO_RAW);
  if (fd < 0) { printf("NETFILTER_PROBE=FAIL stage=socket errno=%d\n",errno); goto out;}
  struct ipt_getinfo info;
  memset(&info,0,sizeof(info));
  memcpy(info.name, "filter", 7);
  socklen_t size=sizeof(info);
  int result=getsockopt(fd,IPPROTO_IP,IPT_SO_GET_INFO,&info,&size);
  if (result==0) printf("NETFILTER_PROBE=GETINFO_PASS hooks=0x%x entries=%u size=%u\n", info.valid_hooks,info.num_entries,info.size);
  else printf("NETFILTER_PROBE=FAIL stage=getsockopt errno=%d\n",errno);
  close(fd);
out:
  sync();
  printf("NETFILTER_PROBE=END\n");
  fflush(stdout);
  reboot(RB_POWER_OFF);
  for (;;)
    pause();
}
