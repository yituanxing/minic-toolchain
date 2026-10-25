/* Real Linux 6.6.143 RV32/ILP32 P2 frontier, freestanding GNU-built
 * userspace on the EXACT 1741/1741 MiniC Linux Image.  A successful
 * P2_FRONTIER means ONLY listed operations succeeded; it is NOT the old
 * 145-module/203-function P3 or the complete RV64 P2 manifest.
 * Probe unsupported CONFIG_* separately, never emit PASS for a skip. */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long word;
#define READ 63
#define WRITE 64
#define CLOSE 57
#define OPENAT 56
#define MKDIRAT 34
#define RENAMEAT 38
#define UNLINKAT 35
#define MOUNT 40
#define SOCKET 198
#define BIND 200
#define LISTEN 201
#define ACCEPT4 242
#define CONNECT 203
#define GETSOCKNAME 204
#define SENDTO 206
#define RECVFROM 207
#define IOCTL 29
#define CLONE 220
#define WAITID 95
#define EXIT 93
#define AF_INET 2
#define AF_ALG 38
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_SEQPACKET 5
#define AT_FDCWD -100
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define SIGCHLD 17
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
static long sc(long n,long x0,long x1,long x2,long x3,long x4,long x5){
 register long a0 __asm__("a0")=x0,a1 __asm__("a1")=x1,
 a2 __asm__("a2")=x2,a3 __asm__("a3")=x3,
 a4 __asm__("a4")=x4,a5 __asm__("a5"),a7 __asm__("a7")=n;
 a5=x5;
 __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7):"memory");
 return a0;
}
#define S0(n) sc(n,0,0,0,0,0,0)
#define S1(n,a) sc(n,(long)(a),0,0,0,0,0)
#define S2(n,a,b) sc(n,(long)(a),(long)(b),0,0,0,0)
#define S3(n,a,b,c) sc(n,(long)(a),(long)(b),(long)(c),0,0,0)
#define S4(n,a,b,c,d) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define S5(n,a,b,c,d,e) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static word len(const char*s){word n=0;while(s[n])n++;return n;}
static void say(const char*s){(void)S3(WRITE,1,s,len(s));}
static void zero(void *ptr,word n){unsigned char*p=ptr;while(n--)*p++=0;}
static int bad(long n){return (word)n >= (word)-4095;}
static void closefd(long n){if(!bad(n))(void)S1(CLOSE,n);}
static int equal(const unsigned char*a,const unsigned char*b,word n){
 for(word i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;
}
static int core_ok=0,core_fail=0,extra_ok=0,extra_unavailable=0,extra_fail=0;
static void mark(const char*name,int status,int core){
 if(status==1){say("RV32I_P2_PASS ");say(name);say("\n");if(core)core_ok++;else extra_ok++;}
 else if(status==0){say("RV32I_P2_UNAVAILABLE ");say(name);say("\n");extra_unavailable++;}
 else{say("RV32I_P2_FAIL ");say(name);say("\n");if(core)core_fail++;else extra_fail++;}
}
static int loopback_up(void){
 unsigned char req[40];zero(req,sizeof(req));req[0]='l';req[1]='o';
 long fd=S3(SOCKET,AF_INET,SOCK_DGRAM,0);
 if(bad(fd))return 0;
 long get=S3(IOCTL,fd,SIOCGIFFLAGS,req);
 if(get==0){
   unsigned short flags=(unsigned short)(req[16]|(req[17]<<8));
   flags|=65;req[16]=flags&255;req[17]=flags>>8;
   get=S3(IOCTL,fd,SIOCSIFFLAGS,req);
 }
 closefd(fd);return get==0;
}
struct sin4{unsigned short family,port;u32 ip;unsigned char padding[8];};
static void set_loopback(struct sin4*a){
 zero(a,sizeof(*a));a->family=AF_INET;a->ip=0x0100007fU;
}
static void tmpfs_rename_test(void){
 const char*start="/tmp/rv32-p2-file",*renamed="/tmp/rv32-p2-renamed";
 long fd=S4(OPENAT,AT_FDCWD,start,O_CREAT|O_WRONLY|O_TRUNC,0600);
 int ok=!bad(fd)&&S3(WRITE,fd,"P2TEST",6)==6;
 closefd(fd);
 if(ok)ok=S4(RENAMEAT,AT_FDCWD,start,AT_FDCWD,renamed)==0;
 fd=S4(OPENAT,AT_FDCWD,renamed,0,0);
 char data[8];zero(data,sizeof(data));
 if(ok)ok=!bad(fd)&&S3(READ,fd,data,6)==6&&
                equal((unsigned char*)data,(const unsigned char*)"P2TEST",6);
 closefd(fd);
 if(ok)ok=S3(UNLINKAT,AT_FDCWD,renamed,0)==0;
 mark("tmpfs-rename-rw",ok?1:-1,1);
}
static void ipv4_udp_test(void){
 struct sin4 addr;set_loopback(&addr);
 if(!loopback_up()){mark("ipv4-udp-loopback",-1,1);return;}
 long receiver=S3(SOCKET,AF_INET,SOCK_DGRAM,0);
 long sender=S3(SOCKET,AF_INET,SOCK_DGRAM,0);
 u32 alen=sizeof(addr);
 int ok=!bad(receiver)&&!bad(sender)&&
     S3(BIND,receiver,&addr,sizeof(addr))==0 &&
     S3(GETSOCKNAME,receiver,&addr,&alen)==0 && alen==sizeof(addr);
 if(ok)ok=S5(SENDTO,sender,"UDP",3,0,&addr)==-22?0:1; /* overwritten below */
 if(ok){
   long sent=sc(SENDTO,sender,(long)"UDP",3,0,(long)&addr,sizeof(addr));
   /* Second datagram is deliberate; read the exact three-byte payload. */
   char got[4]={0,0,0,0};
   long n=S3(RECVFROM,receiver,got,3);
   ok=sent==3&&n==3&&equal((unsigned char*)got,(const unsigned char*)"UDP",3);
 }
 closefd(sender);closefd(receiver);
 mark("ipv4-udp-loopback",ok?1:-1,1);
}
static void ipv4_tcp_test(void){
 struct sin4 addr;set_loopback(&addr);
 long listener=S3(SOCKET,AF_INET,SOCK_STREAM,0);
 u32 alen=sizeof(addr);
 int ok=!bad(listener)&&S3(BIND,listener,&addr,sizeof(addr))==0 &&
  S3(GETSOCKNAME,listener,&addr,&alen)==0&&S2(LISTEN,listener,1)==0;
 if(!ok){closefd(listener);mark("ipv4-tcp-loopback",-1,1);return;}
 long child=S5(CLONE,SIGCHLD,0,0,0,0);
 if(child==0){
   long fd=S3(SOCKET,AF_INET,SOCK_STREAM,0);
   char reply=0;
   int success=!bad(fd)&&S3(CONNECT,fd,&addr,sizeof(addr))==0&&
       S3(WRITE,fd,"T",1)==1&&S3(READ,fd,&reply,1)==1&&reply=='K';
   closefd(fd);(void)S1(EXIT,success?0:1);
   for(;;)__asm__ volatile("nop");
 }
 if(bad(child)){closefd(listener);mark("ipv4-tcp-loopback",-1,1);return;}
 long accepted=S4(ACCEPT4,listener,0,0,0);char received=0;
 int handshake=!bad(accepted)&&S3(READ,accepted,&received,1)==1 &&
       received=='T'&&S3(WRITE,accepted,"K",1)==1;
 closefd(accepted);closefd(listener);
 u32 info[32];zero(info,sizeof(info));
 long waited=S5(WAITID,1,child,info,4,0);
 ok=handshake&&waited==0&&info[2]==1&&info[3]==(u32)child&&info[5]==0;
 mark("ipv4-tcp-loopback",ok?1:-1,1);
}
struct algaddr{
 unsigned short family; char type[14]; u32 feat;u32 mask;char name[64];
};
static void set_str(char*d,const char*s,word max){
 word n=0;while(n+1<max&&s[n]){d[n]=s[n];++n;}d[n]=0;
}
static int digest_test(const char*alg,const unsigned char*expect,word size){
 struct algaddr addr;zero(&addr,sizeof(addr));
 addr.family=AF_ALG;set_str(addr.type,"hash",sizeof(addr.type));
 set_str(addr.name,alg,sizeof(addr.name));
 long fd=S3(SOCKET,AF_ALG,SOCK_SEQPACKET,0);
 if(fd==-97 || fd==-93 || fd==-38)return 0; /* EAFNOSUPPORT, EPROTONOSUPPORT, ENOSYS */
 if(bad(fd))return -1;
 long rc=S3(BIND,fd,&addr,sizeof(addr));
 if(rc==-2 || rc==-19){closefd(fd);return 0;} /* algorithm not registered */
 if(rc!=0){closefd(fd);return -1;}
 long operation=S4(ACCEPT4,fd,0,0,0);
 unsigned char result[64];zero(result,sizeof(result));
 int ok=!bad(operation)&&S3(WRITE,operation,"abc",3)==3&&
    S3(READ,operation,result,size)==(long)size&&equal(result,expect,size);
 closefd(operation);closefd(fd);
 return ok?1:-1;
}
static void crypto_test(void){
 static const unsigned char sha256[32]={
 0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
 0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 static const unsigned char sha3[32]={
 0x3a,0x98,0x5d,0xa7,0x4f,0xe2,0x25,0xb2,0x04,0x5c,0x17,0x2d,0x6b,0xd3,0x90,0xbd,
 0x85,0x5f,0x08,0x6e,0x3e,0x9d,0x52,0x5b,0x46,0xbf,0xe2,0x45,0x11,0x43,0x15,0x32};
 mark("afalg-sha256-abc",digest_test("sha256",sha256,32),0);
 mark("afalg-sha3-256-abc",digest_test("sha3-256",sha3,32),0);
}
static void overlay_test(void){
 /* A real overlay lower/read + upper copy-up and original lower integrity.
  * A missing overlay filesystem is an explicit UNSUPPORTED, not PASS. */
 const char*lo="/tmp/p2-lower",*up="/tmp/p2-upper",
           *wk="/tmp/p2-work",*merged="/tmp/p2-merged";
 const char*source="/tmp/p2-lower/data",*target="/tmp/p2-merged/data",
           *copied="/tmp/p2-upper/data";
 (void)S3(MKDIRAT,AT_FDCWD,lo,0700);
 (void)S3(MKDIRAT,AT_FDCWD,up,0700);
 (void)S3(MKDIRAT,AT_FDCWD,wk,0700);
 (void)S3(MKDIRAT,AT_FDCWD,merged,0700);
 long f=S4(OPENAT,AT_FDCWD,source,O_CREAT|O_WRONLY|O_TRUNC,0600);
 int ok=!bad(f)&&S3(WRITE,f,"LOW",3)==3;closefd(f);
 if(!ok){mark("overlay-copy-up",-1,0);return;}
 static const char options[]="lowerdir=/tmp/p2-lower,upperdir=/tmp/p2-upper,workdir=/tmp/p2-work";
 long rv=S5(MOUNT,"overlay",merged,"overlay",0,options);
 if(rv==-19||rv==-95||rv==-38){mark("overlay-copy-up",0,0);return;}
 if(rv!=0){mark("overlay-copy-up",-1,0);return;}
 char b[4]={0,0,0,0};
 f=S4(OPENAT,AT_FDCWD,target,0,0);
 ok=!bad(f)&&S3(READ,f,b,3)==3&&equal((unsigned char*)b,(const unsigned char*)"LOW",3);
 closefd(f);
 f=S4(OPENAT,AT_FDCWD,target,O_WRONLY|O_TRUNC,0);
 if(ok)ok=!bad(f)&&S3(WRITE,f,"UP!",3)==3;
 closefd(f);
 f=S4(OPENAT,AT_FDCWD,copied,0,0);
 zero(b,sizeof(b));
 if(ok)ok=!bad(f)&&S3(READ,f,b,3)==3&&equal((unsigned char*)b,(const unsigned char*)"UP!",3);
 closefd(f);
 f=S4(OPENAT,AT_FDCWD,source,0,0);
 zero(b,sizeof(b));
 if(ok)ok=!bad(f)&&S3(READ,f,b,3)==3&&equal((unsigned char*)b,(const unsigned char*)"LOW",3);
 closefd(f);
 mark("overlay-copy-up",ok?1:-1,0);
}
__attribute__((noreturn,used)) void _start(void){
 say("RV32I_P2_BEGIN scope=runtime-frontier-not-full-P2\n");
 tmpfs_rename_test();
 ipv4_udp_test();
 ipv4_tcp_test();
 crypto_test();
 overlay_test();
 if(core_fail==0&&core_ok==3&&extra_fail==0){
   say("RV32I_P2_FRONTIER=PASS core=3 optional_capabilities_reported=true\n");
 }else{
   say("RV32I_P2_FRONTIER=FAIL core_or_supported_capability\n");
 }
 (void)S1(EXIT,(core_fail==0&&core_ok==3&&extra_fail==0)?0:1);
 for(;;)__asm__ volatile("nop");
}
