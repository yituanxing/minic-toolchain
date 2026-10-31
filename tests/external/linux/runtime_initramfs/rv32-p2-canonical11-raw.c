/* Linux 6.6.143 genuine RV32I/ILP32 canonical P2 expansion (five rows).
 * Complements six overlapping canonical operations verified by rv32-p2-raw.c.
 * No libc, simulated filesystem, host-side success, or unavailable-as-pass.
 * A full eighteen-row P2 verdict requires seven MORE independent operations.
 */
typedef unsigned int u32;
typedef unsigned long word;
typedef unsigned long long u64;
#define OPENAT 56
#define READ 63
#define WRITE 64
#define CLOSE 57
#define MKDIRAT 34
#define MKNODAT 33
#define IOCTL 29
#define MOUNT 40
#define UMOUNT2 39
#define SYNC 81
#define SOCKET 198
#define GETSOCKOPT 209
#define FINIT_MODULE 273
#define EXIT 93
#define AT_FDCWD -100
#define O_RDONLY 0
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define AF_INET 2
#define AF_INET6 10
#define SOCK_RAW 3
#define IPPROTO_RAW 255
#define IPPROTO_IP 0
#define IPPROTO_IPV6 41
#define IPT_SO_GET_INFO 64
#define IP6T_SO_GET_INFO 64
static long sc(long n,long x0,long x1,long x2,long x3,long x4){
 register long a0 __asm__("a0")=x0,a1 __asm__("a1")=x1,
 a2 __asm__("a2")=x2,a3 __asm__("a3")=x3,a4 __asm__("a4")=x4,
 a7 __asm__("a7")=n;
 __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a7):"memory");
 return a0;
}
#define S0(n) sc(n,0,0,0,0,0)
#define S1(n,a) sc(n,(long)(a),0,0,0,0)
#define S2(n,a,b) sc(n,(long)(a),(long)(b),0,0,0)
#define S3(n,a,b,c) sc(n,(long)(a),(long)(b),(long)(c),0,0)
#define S4(n,a,b,c,d) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),0)
#define S5(n,a,b,c,d,e) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e))
static int bad(long n){return (word)n>=(word)-4095;}
static word slen(const char*s){word n=0;while(s[n])n++;return n;}
static void say(const char*s){(void)S3(WRITE,1,s,slen(s));}
static void zero(void*p,word n){unsigned char*b=p;while(n--)*b++=0;}
static void closefd(long fd){if(!bad(fd))(void)S1(CLOSE,fd);}
static void show_errno(long x){
 static const char digits[]="0123456789abcdef";
 char b[8];word at=8;unsigned long n;
 if(x<0){say("-");n=(unsigned long)(-(x+1))+1;}else n=(unsigned long)x;
 say("0x");do{b[--at]=digits[n&15];n>>=4;}while(n&&at);
 (void)S3(WRITE,1,b+at,8-at);
}
static int equal(const char*a,const char*b,word size){
 for(word i=0;i<size;i++)if(a[i]!=b[i])return 0;
 return 1;
}
static int has(const char*b,word n,const char*needle){
 word m=slen(needle);
 if(m>n)return 0;
 for(word i=0;i+m<=n;i++)if(equal(b+i,needle,m))return 1;
 return 0;
}
static int failures,passes;
static void report(const char*id,int ok,long rc){
 if(ok){say("RV32I_P2_C11_PASS ");say(id);say("\n");passes++;}
 else{say("RV32I_P2_C11_FAIL ");say(id);say(" errno=");show_errno(rc);say("\n");failures++;}
}
static int load_module(const char*name){
 char path[128];const char *prefix="/lib/modules/";word at=0,i=0;
 while(prefix[i]&&at+1<sizeof(path))path[at++]=prefix[i++];
 i=0;while(name[i]&&at+4<sizeof(path))path[at++]=name[i++];
 if(name[i]){say("RV32I_P2_C11_MODULE_FAIL invalid_path\n");return 0;}
 path[at++]='.';path[at++]='k';path[at++]='o';path[at]=0;
 long fd=S4(OPENAT,AT_FDCWD,path,O_RDONLY,0);
 if(bad(fd)){
  say("RV32I_P2_C11_MODULE_FAIL open ");say(name);say(" errno=");show_errno(fd);say("\n");
  return 0;
 }
 long rc=S3(FINIT_MODULE,fd,"",0);closefd(fd);
 if(rc!=0){
  say("RV32I_P2_C11_MODULE_FAIL finit_module ");say(name);say(" errno=");show_errno(rc);say("\n");
  return 0;
 }
 say("RV32I_P2_C11_MODULE_PASS ");say(name);say("\n");return 1;
}
static int modules(void){
 /* Explicit topological order; original Kbuild artifact has no
  * Module.symvers, so modpost can't produce reliable depends= metadata. */
 static const char*const names[]={
  "x_tables","xt_tcpudp","ip_tables","iptable_filter",
  "ip6_tables","ip6table_filter",
  "libcrc32c","xor","raid6_pq","zlib_deflate","lzo_compress",
  "btrfs","dm-mod"
 };
 for(word i=0;i<sizeof(names)/sizeof(names[0]);i++)
  if(!load_module(names[i]))return 0;
 return 1;
}
/* Real ext4 write/sync/unmount/remount/read persistence on attached
 * QEMU virtio-blk /dev/vda, NEVER a tmpfs substitute. */
static void ext4_roundtrip(void){
 const char*dev="/dev/vda",*mnt="/tmp/rv32-p2-ext4";
 const char*file="/tmp/rv32-p2-ext4/roundtrip.txt";
 static const char payload[]="RV32-P2-ext4-persist";
 long fd,rc=S3(MKDIRAT,AT_FDCWD,mnt,0700);
 if(rc!=0&&rc!=-17){report("rootfs-rw",0,rc);return;}
 rc=S5(MOUNT,dev,mnt,"ext4",0,0);
 if(rc!=0){report("rootfs-rw",0,rc);return;}
 fd=S4(OPENAT,AT_FDCWD,file,O_CREAT|O_TRUNC|O_RDWR,0600);
 int ok=!bad(fd)&&S3(WRITE,fd,payload,sizeof(payload))==(long)sizeof(payload);
 closefd(fd);
 (void)S0(SYNC);
 rc=S2(UMOUNT2,mnt,0);
 if(rc!=0){report("rootfs-rw",0,rc);return;}
 rc=S5(MOUNT,dev,mnt,"ext4",0,0);
 if(rc!=0){report("rootfs-rw",0,rc);return;}
 char got[sizeof(payload)];zero(got,sizeof(got));
 fd=S4(OPENAT,AT_FDCWD,file,O_RDONLY,0);
 if(!bad(fd))ok=ok&&S3(READ,fd,got,sizeof(got))==(long)sizeof(got)&&
  equal(got,payload,sizeof(got));
 else ok=0;
 closefd(fd);
 rc=S2(UMOUNT2,mnt,0);
 ok=ok&&rc==0;
 report("rootfs-rw",ok,ok?0:rc);
 if(ok)say("RUNTIME_V2_ROOTFS_RW=PASS\n");
}
struct ipt_getinfo{
 char name[32];u32 valid_hooks,hook_entry[5],underflow[5],num_entries,size;
};
typedef char verify_ipt_size[(sizeof(struct ipt_getinfo)==84)?1:-1];
static void iptables_info(int family,const char*id){
 struct ipt_getinfo inf;zero(&inf,sizeof(inf));
 const char*name="filter";for(word i=0;i<7;i++)inf.name[i]=name[i];
 long fd=S3(SOCKET,family,SOCK_RAW,IPPROTO_RAW);
 if(bad(fd)){report(id,0,fd);return;}
 u32 size=sizeof(inf);
 long rc=S5(GETSOCKOPT,fd,family==AF_INET?IPPROTO_IP:IPPROTO_IPV6,
            family==AF_INET?IPT_SO_GET_INFO:IP6T_SO_GET_INFO,&inf,&size);
 closefd(fd);
 report(id,rc==0&&size==sizeof(inf)&&inf.valid_hooks&&inf.num_entries,
        rc==0?-71:rc);
}
static void btrfs_registration(void){
 char buf[4096];long fd=S4(OPENAT,AT_FDCWD,"/proc/filesystems",O_RDONLY,0);
 if(bad(fd)){report("btrfs",0,fd);return;}
 long n=S3(READ,fd,buf,sizeof(buf));closefd(fd);
 report("btrfs",n>0&&has(buf,(word)n,"\tbtrfs\n"),n<0?n:-19);
}
struct dm_ioctl{
 u32 version[3],data_size,data_start,target_count,open_count,flags,event_nr,padding;
 u64 dev;char name[128],uuid[129],data[7];
};
typedef char verify_dm_size[(sizeof(struct dm_ioctl)==312)?1:-1];
#define DM_VERSION_IOCTL (0xc0000000U|(312U<<16)|(0xfdU<<8))
static void dm_control(void){
 long rc=S3(MKDIRAT,AT_FDCWD,"/dev/mapper",0700);
 if(rc!=0&&rc!=-17){report("dm",0,rc);return;}
 rc=S4(MKNODAT,AT_FDCWD,"/dev/mapper/control",0020000|0600,(10<<8)|236);
 if(rc!=0&&rc!=-17){report("dm",0,rc);return;}
 long fd=S4(OPENAT,AT_FDCWD,"/dev/mapper/control",O_RDWR,0);
 if(bad(fd)){report("dm",0,fd);return;}
 struct dm_ioctl ctl;zero(&ctl,sizeof(ctl));
 ctl.version[0]=4;ctl.version[1]=48;ctl.version[2]=0;
 ctl.data_size=sizeof(ctl);ctl.data_start=sizeof(ctl);
 rc=S3(IOCTL,fd,DM_VERSION_IOCTL,&ctl);closefd(fd);
 report("dm",rc==0&&ctl.version[0]==4,rc==0?-71:rc);
}
__attribute__((noreturn,used)) void _start(void){
 say("RV32I_P2_C11_BEGIN canonical=additional-five-of-eighteen\n");
 if(!modules()){
  say("RV32I_P2_C11_FRONTIER=FAIL required_module_load\n");
  (void)S1(EXIT,1);for(;;)__asm__ volatile("nop");
 }
 ext4_roundtrip();
 iptables_info(AF_INET,"iptables4");
 iptables_info(AF_INET6,"iptables6");
 btrfs_registration();
 dm_control();
 if(passes==5&&failures==0)
  say("RV32I_P2_C11_FRONTIER=PASS old_canonical=6 new_canonical=5 canonical=11/18\n");
 else say("RV32I_P2_C11_FRONTIER=FAIL incomplete_or_failed_capability\n");
 (void)S1(EXIT,(passes==5&&failures==0)?0:1);
 for(;;)__asm__ volatile("nop");
}
