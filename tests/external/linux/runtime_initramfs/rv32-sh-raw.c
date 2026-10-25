/* Minimal serial command shell for the historical RV64 rdinit=/bin/sh
 * console oracle. This is a real RV32I ELF32 Linux userspace process;
 * it reads commands on ttyS0, runs mount/echo/cat/sync/poweroff and
 * requires real QEMU poweroff. NOT BusyBox or a general POSIX shell. */
typedef unsigned long word;
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_MKDIRAT 34
#define SYS_MOUNT 40
#define SYS_SYNC 81
#define SYS_REBOOT 142
#define AT_FDCWD -100
static long sc(long n,long p0,long p1,long p2,long p3,long p4) {
 register long a0 __asm__("a0")=p0,a1 __asm__("a1")=p1,
               a2 __asm__("a2")=p2,a3 __asm__("a3")=p3,
               a4 __asm__("a4")=p4,a7 __asm__("a7")=n;
 __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a7):"memory");
 return a0;
}
#define C0(n) sc(n,0,0,0,0,0)
#define C1(n,a) sc(n,(long)(a),0,0,0,0)
#define C2(n,a,b) sc(n,(long)(a),(long)(b),0,0,0)
#define C3(n,a,b,c) sc(n,(long)(a),(long)(b),(long)(c),0,0)
#define C5(n,a,b,c,d,e) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e))
static word slen(const char *p){word n=0;while(p[n])n++;return n;}
static void say(const char *p){(void)C3(SYS_WRITE,1,p,slen(p));}
static int same(const char*a,const char*b){
 while(*a&&*a==*b){a++;b++;}return *a==*b;
}
static int prefix(const char*a,const char*b){
 while(*b&&*a==*b){a++;b++;}return *b==0;
}
static int report_cmdline(void){
 char buf[1024];
 long fd=C3(SYS_OPENAT,AT_FDCWD,"/proc/cmdline",0);
 if(fd<0)return 0;
 long n=C3(SYS_READ,fd,buf,sizeof(buf));
 (void)C1(SYS_CLOSE,fd);
 if(n<=0)return 0;
 (void)C3(SYS_WRITE,1,buf,n);
 return 1;
}
__attribute__((noreturn,used))void _start(void){
 char line[512];word used=0;
 say("MINIC_LINUX_RUNTIME_SH_READY\n");
 for(;;){
  char c=0;
  long n=C3(SYS_READ,0,&c,1);
  if(n!=1)continue;
  if(c=='\r')continue;
  if(c!='\n'&&used+1<sizeof(line)){line[used++]=c;continue;}
  line[used]=0;used=0;
  if(same(line,"mount -t proc proc /proc")){
   (void)C3(SYS_MKDIRAT,AT_FDCWD,"/proc",0555);
   long rv=C5(SYS_MOUNT,"proc","/proc","proc",0,0);
   if(rv!=0&&rv!=-16)say("RV32_SHELL_MOUNT=FAIL\n");
  }else if(prefix(line,"echo ")){
   say(line+5);say("\n");
  }else if(same(line,"cat /proc/cmdline")){
   if(!report_cmdline())say("RV32_SHELL_CAT=FAIL\n");
  }else if(same(line,"sync")){(void)C0(SYS_SYNC);
  }else if(same(line,"poweroff -f")){
   (void)C0(SYS_SYNC);
   (void)sc(SYS_REBOOT,0xfee1dead,0x28121969,0x4321fedc,0,0);
  }else if(line[0])say("RV32_SHELL_UNKNOWN_COMMAND\n");
 }
}
