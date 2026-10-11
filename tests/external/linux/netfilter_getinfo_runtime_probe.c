/* Focused user-space reproduction of the real Python 145/203 M38
 * "iptables-get-info" operation. This is NOT a replacement for the
 * untouched historical 145/203 release oracle.
 *
 * Build (GNU userland, separate from MiniC kernel): riscv64-linux-gnu-gcc
 * -O2 -static this_file.c -o init
 */
#define _GNU_SOURCE
#include <errno.h>
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

static const char *paths[] = {
  "/lib/modules/6.6.143/net/netfilter/x_tables.ko",
  "/lib/modules/6.6.143/net/ipv4/netfilter/ip_tables.ko",
  "/lib/modules/6.6.143/net/ipv4/netfilter/iptable_filter.ko",
};

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
  printf("NETFILTER_PROBE=BEGIN\n");
  for (size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++)
    if (insert_module(paths[i]) != 0) goto out;
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
