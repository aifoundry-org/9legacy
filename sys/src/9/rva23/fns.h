#include "../port/portfns.h"

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

/* l.s */
extern int tas(void*);
extern int cmpswap(long*, long, long);		/* cas.s */
extern void coherence(void);
extern void idlehands(void);
extern uvlong vcycles(void);
#define cycles(ip) *(ip) = vcycles()
extern void touser(uintptr);
extern void sysrforkret(void);
extern void fpconstset(void);
extern void fpoff(void);
extern uintptr fpstatus(void);
extern void fpsaveregs(void*);
extern void fploadregs(void*);
extern void _fpuinit(void);
extern ulong getfcsr(void);
extern ulong setfcsr(uintptr);
extern int islo(void);
extern void wfi(void);
extern void halt(void);
extern int aadd(int*, int);
extern ulong amoswapw(ulong*, ulong);

extern void flushasidva(uintptr);
extern void flushasidvall(uintptr);
extern void flushasid(uintptr);
extern void flushtlb(void);
extern void flushalltlb(void);
extern void flushvatlb(uintptr);
extern void wsatp(u64int);
extern u64int rsatp(void);
extern u64int rseed(void);
extern void wstvec(u64int);
extern void putscounteren(uintptr);
extern void putsscratch(uintptr);
extern uintptr getsp(void);
extern uintptr setsie(uintptr);
extern uintptr clrsie(uintptr);
extern uintptr getsie(void);
extern void putsie(uintptr);
extern uintptr getsip(void);
extern void putsip(uintptr);
extern ulong clrsipbit(ulong);
extern void clrstie(void);
extern void clockenable(void);
extern uvlong rdtime(void);
extern uvlong rdtsc(void);
extern ulong perfticks(void);
extern void imsicswr(uint, uvlong);
extern uvlong stopeiclaim(void);
extern int peekb(void*);
extern int peekl(void*, u32int*, int);
extern void soft(void);
extern void cpumain(void);
extern uintptr getmsts(void);
extern void putmsts(uintptr);

/* timecmp.s */
extern void wrstimecmp(u64int);
extern u64int rdstimecmp(void);

/* sbi */
extern void sbiputc(uvlong);
extern int sbigetc(void);
extern vlong sbicall(uvlong, uvlong, uvlong, Sbiret*, uvlong*);
extern vlong sbiecall(uvlong, uvlong, uvlong, Sbiret*, uvlong*);
extern vlong sbiclearipi(void);
extern vlong sbigetimplid(void);
extern vlong sbigetimplvers(void);
extern uvlong sbigetmarchid(void);
extern uvlong sbigetmimpid(void);
extern uvlong sbigetmvendorid(void);
extern vlong sbihartstart(uvlong, uvlong, uvlong);
extern vlong sbihartstatus(uvlong);
extern vlong sbihartstop(void);
extern vlong sbihartsuspend(void);
extern vlong sbiprobeext(uvlong);
extern vlong sbiremotesfencevma(uvlong*, uvlong, uvlong);
extern void sbireset(ulong, ulong);
extern vlong sbisendipi(uvlong*);
extern void sbisettimer(uvlong);
extern void sbishutdown(void);

/* cache */
extern void cacheiinvse(void*, int);
extern void cachedwbse(void*, int);
extern void cachedinvse(void*, int);
extern void cachedwbinvse(void*, int);
extern void cboinval(void*);
extern void cboflush(void*);
extern void cboclean(void*);
extern void l2init(void);
extern void dmaflush(int, void*, ulong);

/* mmu */
#define	getpgcolor(a)	0
#define countpagerefs(a, b)
extern uintptr paddr(void*);
#define PADDR(a) paddr((void*)(a))
extern uintptr cankaddr(uintptr);
extern void* kaddr(uintptr);
#define KADDR(a) kaddr(a)
extern void kmapinval(void);
#define	VA(k)	((uintptr)(k))
extern KMap *kmap(Page*);
extern void kunmap(KMap*);
extern void kmapram(uintptr, uintptr);
extern void* vmap(uvlong, vlong);
extern void vunmap(void*, vlong);
extern void mmu1init(void);
extern void* mmuuncache(void*, usize);
extern void putasid(Proc*);
extern u64int* bootptpage(void);

extern void* ucalloc(usize);
extern void* ucallocalign(usize, int, usize);
extern void ucfree(void*);

/* mem */
extern void meminit(void);


/* clock */
extern void clockinit(void);
extern void clockshutdown(void);
extern void clockoff(void);
extern void timerset(uvlong);
extern int clocksanity(void);
extern void calibrate(void);

extern void syscallfmt(int, ulong, va_list);
extern void sysretfmt(int, va_list, long, uvlong, uvlong);

/* fpu */
extern void fpuinit(void);
extern void fpuforkchild(Proc*, Ureg*);
extern void fpunotify(void);
extern void fpunoted(void);
extern void fpusave(void);
extern void mathtrap(Ureg*);
extern void fpsts2ureg(Ureg*);

/* trap */
extern void trapinit(void);
extern int userureg(Ureg*);
extern void validalign(uintptr, unsigned);
extern void procfork(Proc*);
extern void procsetup(Proc*);
extern void procsave(Proc*);
extern void procrestore(Proc*);
extern void trap(Ureg*);
extern void syscall(Ureg*);
extern void dumpregs(Ureg*);
extern void clearipi(void);
extern uint mach2context(Mach*);

/* syscall */
extern void kexit(Ureg*);
extern void forkret(void);

/* intr */
extern void intrinit(void);
extern void socidentify(void);
extern void intrcpushutdown(void);
extern void intrsoff(void);
extern void intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void intrdisable(int, void (*)(Ureg*, void*), void*, int, char*);
extern int irq(Ureg*);
extern void plicinit(void);
extern int plicirq(Ureg*);
extern void plicintrenable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void plicintrdisable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void aplicinit(void);
extern int aplicirq(Ureg*);
extern void aplicintrenable(int, void (*)(Ureg*, void*), void*, int, char*);
extern void aplicintrdisable(int, void (*)(Ureg*, void*), void*, int, char*);

/* uart */
extern void uartconsinit(void);

/* main */
extern char* getconf(char*);
extern void setconfenv(void);
extern void writeconf(void);
extern int isaconfig(char*, int, ISAConf*);
extern void links(void);
extern void poweroff(void);
extern void exit(int);

/* devarch */
extern Dirtab* addarchfile(char*, int,
	long(*)(Chan*,void*,long,vlong), long(*)(Chan*,void*,long,vlong));

/* pciqemu */
extern int pcicfgrw8(int, int, int, int);
extern int pcicfgrw16(int, int, int, int);
extern int pcicfgrw32(int, int, int, int);
extern void pciintrenable(int, void (*)(Ureg*, void*), void*);
extern void pciintrdisable(int, void (*)(Ureg*, void*), void*);

/* bootargs */
extern void bootargsinit(void);
extern int dtbnuart(void);
extern Dtbuart* dtbuart(int);
extern int dtbhasplic(void);
extern int dtbnether(void);
extern Dtbether* dtbether(int);
extern int dtbnufs(void);
extern Dtbufs* dtbufs(int);
extern int dtbsocmatch(char*);

/* reloc */
extern void relocfixup(void);

/* archrv */
extern void archreset(void);
extern void cpuinit(int);
extern void cpuidprint(void);
extern void clintipitohart(int);
extern int ipitohart(int);
extern void countipi(void);
extern void idlewake(void);
extern void mpinit(void);
extern void hsmstartall(void);
extern void cpustart(int);
extern void archfmtinstall(void);
