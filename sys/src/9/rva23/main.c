#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "pool.h"
#include "io.h"
#include "sysreg.h"
#include "ureg.h"
#include "init.h"

Conf conf;

int
isaconfig(char *, int, ISAConf *)
{
	return 0;
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	char buf[2*KNAMELEN];

	up->nerrlab = 0;
	coherence();
	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "RISCV64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "riscv64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		setconfenv();
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	/* initcode's main builds its own argv; it needs only a stack */
	splhi();
	fpoff();
	touser(STACKALIGN(USTKTOP-sizeof(Tos)-64));
	assert(0);			/* shouldn't have returned */
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->s.args)-sizeof(uintptr));
	p->sched.sp = STACKALIGN(p->sched.sp);

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	s->flushme++;
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
confinit(void)
{
	int userpcnt;
	ulong kpages;
	char *p;
	int i;

	sys = &asys;
	sys->nonline = 1;		/* the boot cpu; secondaries bump this */

	/*
	 * Default to every hart the DTB lists (hartcnt, counted in bootargs.c);
	 * *ncpu pins a smaller count, e.g. *ncpu=1.  Uniprocessor if the DTB
	 * gave us no cpu nodes.
	 */
	conf.nmach = hartcnt > 0? hartcnt: 1;
	if(p = getconf("*ncpu"))
		conf.nmach = strtol(p, 0, 0);
	if(conf.nmach > MAXMACH)
		conf.nmach = MAXMACH;
	if(conf.nmach < 1)
		conf.nmach = 1;

	if(p = getconf("service")){
		if(strcmp(p, "cpu") == 0)
			cpuserver = 1;
		else if(strcmp(p,"terminal") == 0)
			cpuserver = 0;
	}

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	if(userpcnt < 10)
		userpcnt = 60 + cpuserver*10;
	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	kpages = conf.npage - (conf.npage*userpcnt)/100;
	/*
	 * the pool allocators size in ulong bytes, so the kernel's
	 * share must stay below 4 GiB or the arithmetic here and
	 * mainmem->maxsize wrap; 2 GiB is plenty.
	 */
	if(kpages > (2ULL*GiB)/BY2PG)
		kpages = (2ULL*GiB)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = conf.nmach > 1;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for.
	 */
	kpages = conf.npage - conf.upages;
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap
		+ conf.nswppo*sizeof(Page*);
	mainmem->maxsize = kpages;
	imagmem->maxsize = kpages;
}

void
machinit(void)
{
	extern void strap(void);

	wstvec((uintptr)strap);
	/*
	 * strap() begins by swapping R(MACH) with SSCRATCH, so SSCRATCH
	 * must already hold this hart's Mach pointer before any trap is taken.
	 */
	putsscratch((uintptr)m);
	m->ticks = 1;
	m->perf.period = 1;
	lock(&active);
	cpuactive(m->machno);
	unlock(&active);
}

void
mpinit(void)
{
	/*
	 * hsmstartall() starts the other harts at cpumain (l.s)
	 * via SBI HSM; each lands in cpustart().
	 */
	if(conf.nmach <= 1)
		return;
	if(nosbi){
		print("mpinit: no SBI HSM; staying uniprocessor\n");
		return;
	}
	soc.idlewake = 1;	/* enable cross-cpu wakeup via software ipis */
	hsmstartall();
}

void
cpuidprint(void)
{
	iprint("cpu%d: riscv64 hart %d\n", m->machno, m->hartid);
}

static void
sbiputs(char *s)
{
	int c;

	for(; *s != 0; s++){
		c = *s;
		if(c == '\r')
			sbiputc('\n');
		sbiputc(c);
	}
}

static int once;

void
main(u64int hartid, uintptr dtb)
{
	extern void _start(void);
	extern uintptr dtbphys;
	extern char bdata[], edata[], end[], etext[];
	static ulong vfy = 0xcafebabe;

	dtbphys = dtb;

	if(hartid == 0 && !once){
		/*
		 * Zero every per-cpu Mach page, not just cpu0's: they are
		 * not in bss and cpumain doesn't clear them.  The Mach pages
		 * stack downward from just below KTZERO, so MACHP(MAXMACH-1)
		 * is the lowest.  cpu0's boot stack and L1TOP live below it.
		 */
		memset(MACHP(MAXMACH-1), 0, KTZERO-(uintptr)MACHP(MAXMACH-1));
	}

	if(hartid != 0){
		/*
		 * SBI cold-booted a hart other than 0.  pid 1 is wired to
		 * cpu0 == hart 0, so HSM-start hart 0 at _start, forwarding
		 * the DTB, and stop ourselves.  print() isn't up yet.
		 */
		sbiputs("boot hart != 0; handing off to hart 0\r\n");
		if(sbihartstart(0, PADDR(_start), dtb) < 0)
			sbiputs("sbihartstart(hart 0) failed\r\n");
		sbihartstop();			/* no return on success */
		sbiputs("sbihartstop returned; hanging\r\n");
		for(;;)
			;
	}

	if(once)
		panic("main entered twice");
	once++;
	if(vfy != 0xcafebabe)
		memmove(bdata, etext, edata - bdata);
	if(vfy != 0xcafebabe){
		sbiputc('?');
		panic("misaligned data segment");
	}
	vfy = 0;
	memset(edata, 0, end - edata);		/* zero bss */
	machinit();
	uartconsinit();
	quotefmtinstall();
	bootargsinit();
	meminit();
	confinit();
	xinit();
	printinit();
	print("\nPlan 9\n");
	socidentify();		/* must precede intrinit: sets soc.plic */
	intrinit();
	clockinit();
	if(timebase == 0){
		print("timebase not in DTB, falling back to 10MHz\n");
		timebase = 10*Mhz;
	}
	calibrate();
	clocksanity();
	cpuidprint();
	fpuinit();
	timersinit();
	l2init();	/* Zicbom / l2 cache before any dma-capable device resets */
	pageinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	userinit();

	mpinit();
	mmu1init();
	cpuinit(0);
	/*
	 * cpuinit() leaves the timer disabled (clockoff), and on cpu0
	 * it runs after timersinit(): re-arm the clock.
	 */
	timerset(0);
	schedinit();
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1ULL<<m->machno)) == 0)
		active.ispanic = 0;
	once = (active.machs & (1ULL<<m->machno)) != 0;
	cpuinactive(m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once) {
		delay(m->machno*100);		/* stagger them */
		iprint("cpu%d: exiting\n", m->machno);
	}
	spllo();
	if (m->machno == 0)
		ms = 5*1000;
	else
		ms = 2*1000;
	for(; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}
	if(active.ispanic){
		if(!cpuserver)
			for(;;)
				;
		if(getconf("*debug"))
			delay(5*60*1000);
		else
			delay(10000);
	}
}

void
exit(int code)
{
	shutdown(code);
	splhi();
	if(m->machno != 0)
		for(;;)
			wfi();
	sbireset(1, 0);		/* SRSTCOLD */
	sbireset(2, 0);		/* SRSTWARM */
	sbishutdown();		/* legacy ext 8 */
	for(;;)
		;
}

void
poweroff(void)
{
	splhi();
	sbireset(0, 0);		/* SRSTSHUT */
	sbishutdown();		/* legacy ext 8 */
	for(;;)
		;
}

void
reboot(void*, void*, ulong)
{
	error("reboot not supported");
}

void
dmaflush(int clean, void *p, ulong len)
{
	uintptr s, e;

	s = (uintptr)p;
	e = (uintptr)p + len;
	if(clean){
		s &= ~(BLOCKALIGN-1);
		e += BLOCKALIGN-1;
		e &= ~(BLOCKALIGN-1);
		cachedwbse((void*)s, e - s);
		return;
	}
	if(s & BLOCKALIGN-1){
		s &= ~(BLOCKALIGN-1);
		cachedwbinvse((void*)s, BLOCKALIGN);
		s += BLOCKALIGN;
	}
	if(e & BLOCKALIGN-1){
		e &= ~(BLOCKALIGN-1);
		if(e < s)
			return;
		cachedwbinvse((void*)e, BLOCKALIGN);
	}
	if(s < e)
		cachedinvse((void*)s, e - s);
}
