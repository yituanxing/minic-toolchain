/* Seven *remaining* canonical Linux 6.6.143 RV32I P2 checks.
 * Genuine ELF32/ILP32 raw syscalls (no unavailable-as-pass, no ELF64 libc).
 * Companion canonical11 + original nine are independently checked by PID1.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned long word;
typedef unsigned long long u64;
#define READ 63
#define WRITE 64
#define CLOSE 57
#define OPENAT 56
#define IOCTL 29
#define MKNODAT 33
#define MKDIRAT 34
#define LSEEK 62
#define GETDENTS64 61
#define READLINKAT 78
#define UNSHARE 97
#define NANOSLEEP 101
#define SOCKET 198
#define BIND 200
#define GETSOCKNAME 204
#define SENDTO 206
#define RECVFROM 207
#define CLONE 220
#define WAITID 95
#define SETSOCKOPT 208
#define FINIT_MODULE 273
#define EXIT 93
#define AF_INET 2
#define AF_NETLINK 16
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define NETLINK_ROUTE 0
#define AT_FDCWD -100
#define O_RDONLY 0
#define O_RDWR 2
#define O_DIRECTORY 0200000
#define CLONE_NEWNET 0x40000000
#define SIGCHLD 17
#define IFF_UP 1
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFINDEX 0x8933
#define NLMSG_ERROR 2
#define RTM_NEWLINK 16
#define RTM_DELLINK 17
#define NLM_F_REQUEST 1
#define NLM_F_ACK 4
#define NLM_F_CREATE 0x400
#define NLM_F_EXCL 0x200
#define IFLA_IFNAME 3
#define IFLA_MASTER 10
#define IFLA_LINKINFO 18
#define IFLA_INFO_KIND 1
#define IFLA_INFO_DATA 2
#define VETH_INFO_PEER 1
#define KVM_GET_API_VERSION 0xae00
#define KVM_API_VERSION 12
#define DT_LNK 10
#define DT_DIR 4
static long sc(long n,long x0,long x1,long x2,long x3,long x4,long x5){
 register long a0 __asm__("a0")=x0,a1 __asm__("a1")=x1,
 a2 __asm__("a2")=x2,a3 __asm__("a3")=x3,
 a4 __asm__("a4")=x4,a5 __asm__("a5")=x5,a7 __asm__("a7")=n;
 __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7):"memory");
 return a0;
}
#define S0(n) sc(n,0,0,0,0,0,0)
#define S1(n,a) sc(n,(long)(a),0,0,0,0,0)
#define S2(n,a,b) sc(n,(long)(a),(long)(b),0,0,0,0)
#define S3(n,a,b,c) sc(n,(long)(a),(long)(b),(long)(c),0,0,0)
#define S4(n,a,b,c,d) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),0,0)
#define S5(n,a,b,c,d,e) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
#define S6(n,a,b,c,d,e,f) sc(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),(long)(f))
static int bad(long x){return (word)x>=(word)-4095;}
static word slen(const char*s){word n=0;while(s[n])n++;return n;}
static void say(const char*s){(void)S3(WRITE,1,s,slen(s));}
static void zero(void*p,word n){unsigned char*b=p;while(n--)*b++=0;}
static void closefd(long fd){if(!bad(fd))(void)S1(CLOSE,fd);}
static int equals(const char*a,const char*b,word n){for(word i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static int has(const char*a,word n,const char*b){word m=slen(b);if(m>n)return 0;for(word i=0;i+m<=n;i++)if(equals(a+i,b,m))return 1;return 0;}
static int starts(const char*a,const char*b){word n=slen(b);return equals(a,b,n);}
static int same(const char*a,const char*b){return slen(a)==slen(b)&&equals(a,b,slen(a));}
static void show_num(long x){
 static const char digits[]="0123456789abcdef";
 char b[8];word at=8;unsigned long n;
 if(x<0){say("-");n=(unsigned long)(-(x+1))+1;}else n=(unsigned long)x;
 say("0x");do{b[--at]=digits[n&15];n>>=4;}while(n&&at);
 (void)S3(WRITE,1,b+at,8-at);
}
static int passed,failed;
static void mark(const char*id,int ok,long err){
 if(ok){
  say("RV32I_P2_F18_PASS ");say(id);say("\n");passed++;
  const char*canonical=0;
  if(same(id,"conntrack"))canonical="conntrack-entry-created";
  else if(same(id,"bridge"))canonical="bridge-links-up";
  else if(same(id,"br-netfilter"))canonical="br-netfilter-ipv6-sysctl-toggle";
  else if(same(id,"veth"))canonical="veth-pair-created";
  else if(same(id,"kvm"))canonical="kvm-device-created";
  else if(same(id,"virtio-gpu"))canonical="virtio-gpu-driver-registered";
  else if(same(id,"nvme"))canonical="nvme-pci-driver-registered";
  if(canonical){say("FUNCTION_PASS ");say(canonical);say("\n");}
 }
 else{say("RV32I_P2_F18_FAIL ");say(id);say(" errno=");show_num(err);say("\n");failed++;}
}
static int load_module(const char*name){
 char path[128];const char*pfx="/lib/modules/";word at=0,i=0;
 while(pfx[i]&&at+1<sizeof(path))path[at++]=pfx[i++];
 i=0;while(name[i]&&at+4<sizeof(path))path[at++]=name[i++];
 if(name[i]){say("RV32I_P2_F18_MODULE_FAIL path_overflow\n");return 0;}
 path[at++]='.';path[at++]='k';path[at++]='o';path[at]=0;
 long fd=S4(OPENAT,AT_FDCWD,path,O_RDONLY,0);
 if(bad(fd)){say("RV32I_P2_F18_MODULE_FAIL open ");say(name);say(" errno=");show_num(fd);say("\n");return 0;}
 /* nf_conntrack defaults to packet hooks DISABLED when loaded via raw
  * finit_module: an Image cmdline option alone does not supply parameters
  * to a later module syscall. Require real hooks for isolated UDP 0->1. */
 const char*params=same(name,"nf_conntrack")?"enable_hooks=1":"";
 long rc=S3(FINIT_MODULE,fd,params,0);closefd(fd);
 if(rc){say("RV32I_P2_F18_MODULE_FAIL finit_module ");say(name);say(" errno=");show_num(rc);say("\n");return 0;}
 say("RV32I_P2_F18_MODULE_PASS ");say(name);say("\n");return 1;
}
static int modules(void){
 /* Closure and ordering derived from GNU ELF32 nm references, not empty
  * modinfo depends fields (original artifact has no Module.symvers). */
 static const char*const names[]={
  "nf_defrag_ipv4","nf_defrag_ipv6","nf_conntrack",
  "llc","stp","bridge","br_netfilter","veth",
  /* drm.ko GNU undefined symbols include devm_of_find_backlight,
   * drm_get_panel_orientation_quirk, i2c_transfer (all =m in original
   * Image Kconfig). Load their real symbol-providing modules FIRST. */
  "kvm","i2c-core","backlight","drm_panel_orientation_quirks",
  "drm","drm_kms_helper","drm_shmem_helper","virtio-gpu",
  "nvme-core","nvme"
 };
 for(word i=0;i<sizeof(names)/sizeof(names[0]);i++)
  if(!load_module(names[i]))return 0;
 return 1;
}

struct sin4{u16 family,port;u32 addr;unsigned char pad[8];};
static void lo(struct sin4*a){zero(a,sizeof(*a));a->family=AF_INET;a->addr=0x0100007f;}
static int lo_up(void){
 unsigned char req[40];zero(req,sizeof(req));req[0]='l';req[1]='o';
 long fd=S3(SOCKET,AF_INET,SOCK_DGRAM,0);if(bad(fd))return 0;
 long rc=S3(IOCTL,fd,SIOCGIFFLAGS,req);
 if(!rc){u16 flags=(u16)(req[16]|(req[17]<<8));flags|=65;
  req[16]=flags&255;req[17]=flags>>8;rc=S3(IOCTL,fd,SIOCSIFFLAGS,req);}
 closefd(fd);return rc==0;
}
static int ct_count(u32*count){
 char buf[48];long fd=S4(OPENAT,AT_FDCWD,"/proc/sys/net/netfilter/nf_conntrack_count",O_RDONLY,0);
 if(bad(fd))return 0;
 long n=S3(READ,fd,buf,sizeof(buf));closefd(fd);
 if(n<1)return 0;
 u32 v=0;word i=0;
 while(i<(word)n&&buf[i]>='0'&&buf[i]<='9'){v=v*10+(u32)(buf[i]-'0');i++;}
 if(!i||(i<(word)n&&buf[i]!='\n'))return 0;
 *count=v;return 1;
}
static int conntrack_isolated(void){
 if(S1(UNSHARE,CLONE_NEWNET)!=0||!lo_up())return 0;
 u32 before=0,after=0;
 if(!ct_count(&before)||before!=0)return 0;
 struct sin4 addr;lo(&addr);u32 length=sizeof(addr);
 long r=S3(SOCKET,AF_INET,SOCK_DGRAM,0),s=S3(SOCKET,AF_INET,SOCK_DGRAM,0);
 int ok=!bad(r)&&!bad(s)&&S3(BIND,r,&addr,sizeof(addr))==0&&
  S3(GETSOCKNAME,r,&addr,&length)==0&&length==sizeof(addr);
 if(ok){
  char data[6]={0,0,0,0,0,0};
  ok=S6(SENDTO,s,"ct-p2",5,0,&addr,sizeof(addr))==5&&
     S3(RECVFROM,r,data,5)==5&&equals(data,"ct-p2",5);
 }
 closefd(r);closefd(s);
 if(!ok)return 0;
 for(int attempt=0;attempt<10;attempt++){
  if(!ct_count(&after))return 0;
  if(after==1)break;
  long pause[2]={0,20000000};(void)S2(NANOSLEEP,pause,0);
 }
 if(before==0&&after==1){say("RV32I_P2_F18_CONNTRACK_TRACE before=0 after=1\n");return 1;}
 return 0;
}
static void conntrack_test(void){
 long child=S5(CLONE,SIGCHLD,0,0,0,0);
 if(child==0){int ok=conntrack_isolated();(void)S1(EXIT,ok?0:1);for(;;)__asm__ volatile("nop");}
 if(bad(child)){mark("conntrack",0,child);return;}
 u32 info[32];zero(info,sizeof(info));
 long rc=S5(WAITID,1,child,info,4,0);
 mark("conntrack",rc==0&&info[2]==1&&info[3]==(u32)child&&info[5]==0,rc?rc:-71);
}

struct nlmsghdr{u32 len;u16 type,flags;u32 seq,pid;};
struct ifinfo{unsigned char family,pad;u16 type;int index;u32 flags,change;};
struct rtattr{u16 len,type;};
struct linkreq{struct nlmsghdr hdr;struct ifinfo info;unsigned char attrs[1024];};
struct sockaddr_nl{u16 family,pad;u32 pid,groups;};
static u32 align4(u32 n){return (n+3)&~3U;}
static void req_begin(struct linkreq*q,u16 op,u16 flags,int index){
 zero(q,sizeof(*q));q->hdr.len=sizeof(q->hdr)+sizeof(q->info);
 q->hdr.type=op;q->hdr.flags=NLM_F_REQUEST|NLM_F_ACK|flags;
 q->hdr.seq=1;q->info.index=index;
}
static struct rtattr*attr(struct linkreq*q,u16 type,const void*value,u32 size){
 u32 at=align4(q->hdr.len),end=at+align4(4+size);
 if(end>sizeof(*q)||size>65531)return 0;
 struct rtattr*p=(struct rtattr*)((char*)q+at);
 p->type=type;p->len=(u16)(4+size);
 if(value){const char*from=value;char*to=(char*)p+4;for(u32 i=0;i<size;i++)to[i]=from[i];}
 q->hdr.len=end;return p;
}
static void nest_end(struct linkreq*q,struct rtattr*p){
 p->len=(u16)((char*)q+q->hdr.len-(char*)p);
}
static long rtnl(struct linkreq*q){
 long fd=S3(SOCKET,AF_NETLINK,SOCK_RAW,NETLINK_ROUTE);if(bad(fd))return fd;
 struct sockaddr_nl dst;zero(&dst,sizeof(dst));dst.family=AF_NETLINK;
 long rc=S6(SENDTO,fd,q,q->hdr.len,0,&dst,sizeof(dst));
 if(rc!=(long)q->hdr.len){closefd(fd);return bad(rc)?rc:-5;}
 unsigned char buf[4096];
 rc=S3(RECVFROM,fd,buf,sizeof(buf));closefd(fd);
 if(rc<20)return bad(rc)?rc:-71;
 word off=0;
 while(off+20<=(word)rc){
  struct nlmsghdr*head=(struct nlmsghdr*)(buf+off);
  if(head->len<20||off+head->len>(word)rc)return -71;
  if(head->seq==q->hdr.seq&&head->type==NLMSG_ERROR){
   int err=*(int*)(buf+off+16);return err;
  }
  off+=align4(head->len);
 }
 return -71;
}
static long create_bridge(void){
 struct linkreq q;req_begin(&q,RTM_NEWLINK,NLM_F_CREATE|NLM_F_EXCL,0);
 if(!attr(&q,IFLA_IFNAME,"p2br0",7))return -90;
 struct rtattr*nest=attr(&q,IFLA_LINKINFO,0,0);
 if(!nest||!attr(&q,IFLA_INFO_KIND,"bridge",7))return -90;
 nest_end(&q,nest);return rtnl(&q);
}
static long create_veth(void){
 struct linkreq q;req_begin(&q,RTM_NEWLINK,NLM_F_CREATE|NLM_F_EXCL,0);
 if(!attr(&q,IFLA_IFNAME,"p2v0",5))return -90;
 struct rtattr*info=attr(&q,IFLA_LINKINFO,0,0);
 if(!info||!attr(&q,IFLA_INFO_KIND,"veth",5))return -90;
 struct rtattr*data=attr(&q,IFLA_INFO_DATA,0,0);
 struct ifinfo peer;zero(&peer,sizeof(peer));
 struct rtattr*p=data?attr(&q,VETH_INFO_PEER,&peer,sizeof(peer)):0;
 if(!p||!attr(&q,IFLA_IFNAME,"p2v1",5))return -90;
 nest_end(&q,p);nest_end(&q,data);nest_end(&q,info);return rtnl(&q);
}
static long set_up(u32 index){
 struct linkreq q;req_begin(&q,RTM_NEWLINK,0,(int)index);
 q.info.flags=IFF_UP;q.info.change=IFF_UP;return rtnl(&q);
}
static long set_master(u32 child,u32 master){
 struct linkreq q;req_begin(&q,RTM_NEWLINK,0,(int)child);
 if(!attr(&q,IFLA_MASTER,&master,sizeof(master)))return -90;
 return rtnl(&q);
}
static long delete_link(u32 index){struct linkreq q;req_begin(&q,RTM_DELLINK,0,(int)index);return rtnl(&q);}
static u32 ifindex(const char*name){
 unsigned char req[40];zero(req,sizeof(req));
 word n=slen(name);if(n>=16)return 0;
 for(word i=0;i<n;i++)req[i]=name[i];
 long fd=S3(SOCKET,AF_INET,SOCK_DGRAM,0);if(bad(fd))return 0;
 long rc=S3(IOCTL,fd,SIOCGIFINDEX,req);closefd(fd);
 return rc==0?*(u32*)(req+16):0;
}
static int ifup(const char*name){
 unsigned char req[40];zero(req,sizeof(req));
 word n=slen(name);if(n>=16)return 0;
 for(word i=0;i<n;i++)req[i]=name[i];
 long fd=S3(SOCKET,AF_INET,SOCK_DGRAM,0);if(bad(fd))return 0;
 long rc=S3(IOCTL,fd,SIOCGIFFLAGS,req);closefd(fd);
 return rc==0&&(req[16]&1);
}
static int toggle_brnetfilter(void){
 const char*path="/proc/sys/net/bridge/bridge-nf-call-ip6tables";
 long fd=S4(OPENAT,AT_FDCWD,path,O_RDWR,0);if(bad(fd))return 0;
 char old=0,cur=0;int ok=S3(READ,fd,&old,1)==1&&(old=='0'||old=='1');
 char desired=old=='0'?'1':'0';
 if(ok)ok=S3(LSEEK,fd,0,0)==0&&S3(WRITE,fd,&desired,1)==1&&
  S3(LSEEK,fd,0,0)==0&&S3(READ,fd,&cur,1)==1&&cur==desired;
 int restored=S3(LSEEK,fd,0,0)==0&&S3(WRITE,fd,&old,1)==1&&
  S3(LSEEK,fd,0,0)==0&&S3(READ,fd,&cur,1)==1&&cur==old;
 closefd(fd);return ok&&restored;
}
static void bridge_veth_tests(void){
 long br_rc=create_bridge(),v_rc=create_veth();
 u32 br=ifindex("p2br0"),left=ifindex("p2v0"),right=ifindex("p2v1");
 int vgood=v_rc==0&&left&&right&&left!=right&&set_up(left)==0&&
  set_up(right)==0&&ifup("p2v0")&&ifup("p2v1");
 int bgood=br_rc==0&&br&&vgood&&set_master(left,br)==0&&
  set_up(br)==0&&ifup("p2br0");
 char link[256];long length=bgood?S4(READLINKAT,AT_FDCWD,"/sys/class/net/p2v0/master",link,sizeof(link)):-1;
 bgood=bgood&&length>0&&has(link,(word)(length>0?length:0),"/p2br0");
 int ngood=bgood&&toggle_brnetfilter();
 /* Cleanup is part of the proof. Neither interface may survive. */
 if(left&&delete_link(left)!=0)vgood=0;
 if(br&&delete_link(br)!=0)bgood=0;
 if(ifindex("p2v0")||ifindex("p2v1"))vgood=0;
 if(ifindex("p2br0"))bgood=0;
 mark("br-netfilter",ngood&&bgood,ngood?0:-19);
 mark("veth",vgood,v_rc?v_rc:-71);
 mark("bridge",bgood,br_rc?br_rc:-71);
}
static void kvm_test(void){
 long fd=S4(OPENAT,AT_FDCWD,"/dev/kvm",O_RDWR,0);
 if(fd==-2){
  (void)S4(MKNODAT,AT_FDCWD,"/dev/kvm",0020000|0600,(10<<8)|232);
  fd=S4(OPENAT,AT_FDCWD,"/dev/kvm",O_RDWR,0);
 }
 if(bad(fd)){mark("kvm",0,fd);return;}
 long version=S3(IOCTL,fd,KVM_GET_API_VERSION,0);closefd(fd);
 mark("kvm",version==KVM_API_VERSION,version==KVM_API_VERSION?0:version);
}
/* Read directory entries and require a bound driver link AND child of the
 * same physical device. A mere driver name, module load, or PCI bus is not
 * evidence of hardware-driver binding. */
static int dir_child(const char*path,const char*prefix){
 char bytes[4096];long fd=S4(OPENAT,AT_FDCWD,path,O_RDONLY|O_DIRECTORY,0);
 if(bad(fd))return 0;
 int found=0;
 for(;;){
  long n=S3(GETDENTS64,fd,bytes,sizeof(bytes));if(n<=0)break;
  word at=0;
  while(at+19<=(word)n){
   u16 reclen=*(u16*)(bytes+at+16);
   if(reclen<20||at+reclen>(word)n)break;
   char*name=bytes+at+19;
   if(bytes[at+18]==DT_DIR&&starts(name,prefix)&&
      name[slen(prefix)]>='0'&&name[slen(prefix)]<='9'){
    found=1;break;
   }
   at+=reclen;
  }
  if(found)break;
 }
 closefd(fd);return found;
}
static int add(char*dest,word cap,const char*src){
 word len=slen(dest),n=slen(src);if(len+n+1>cap)return 0;
 for(word i=0;i<=n;i++)dest[len+i]=src[i];
 return 1;
}
static int bound_device(const char*driver,const char*devpath,
                        const char*devprefix,const char*sub,const char*child,
                        int virtio){
 char bytes[4096];long fd=S4(OPENAT,AT_FDCWD,driver,O_RDONLY|O_DIRECTORY,0);
 if(bad(fd))return 0;
 int found=0;
 for(;;){
  long n=S3(GETDENTS64,fd,bytes,sizeof(bytes));if(n<=0)break;
  word at=0;
  while(at+19<=(word)n){
   u16 reclen=*(u16*)(bytes+at+16);
   if(reclen<20||at+reclen>(word)n)break;
   char*name=bytes+at+19;
   int match=bytes[at+18]==DT_LNK;
   if(virtio){
    match=match&&starts(name,devprefix)&&
      name[slen(devprefix)]>='0'&&name[slen(devprefix)]<='9';
   }else match=match&&has(name,slen(name),":")&&has(name,slen(name),".");
   if(match){
    char check[320];check[0]=0;
    if(add(check,sizeof(check),devpath)&&add(check,sizeof(check),"/")&&
       add(check,sizeof(check),name)&&
       (!virtio||add(check,sizeof(check),"/.."))&&
       add(check,sizeof(check),"/")&&add(check,sizeof(check),sub)&&
       dir_child(check,child)){found=1;break;}
   }
   at+=reclen;
  }
  if(found)break;
 }
 closefd(fd);return found;
}
static void device_tests(void){
 mark("virtio-gpu",bound_device("/sys/bus/virtio/drivers/virtio_gpu",
       "/sys/bus/virtio/devices","virtio","drm","card",1),-19);
 mark("nvme",bound_device("/sys/bus/pci/drivers/nvme",
       "/sys/bus/pci/devices","","nvme","nvme",0),-19);
}
__attribute__((noreturn,used)) void _start(void){
 say("RV32I_P2_F18_BEGIN canonical_remaining=7\n");
 if(!modules()){
  say("RV32I_P2_F18_FRONTIER=FAIL required_module_load\n");
  (void)S1(EXIT,1);for(;;)__asm__ volatile("nop");
 }
 conntrack_test();
 bridge_veth_tests();
 kvm_test();
 device_tests();
 if(passed==7&&failed==0)say("RV32I_P2_F18_FRONTIER=PASS new_canonical=7 old_canonical=11 full=18/18 fail=0 skip=0\n");
 else say("RV32I_P2_F18_FRONTIER=FAIL incomplete_or_failed_capability\n");
 (void)S1(EXIT,(passed==7&&failed==0)?0:1);
 for(;;)__asm__ volatile("nop");
}
