#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"
#include "sysreg.h"
#include "riscv64.h"

/* instruction decoding */
#define UNCOMPINST(pc)	(*(ushort *)(pc) | *(ushort *)((pc) + 2) << 16)
#define BASEOP(inst)	((inst) & MASK(7))

enum {
	Trapdebug	= 0,
};
enum Faulttypes {
	Unknownflt, Exception, Localintr, Globalintr, Nfaulttypes,
};

/* print what looks like return pcs on the stack, for ktrace or acid */
static void
_dumpstack(Ureg *ureg)
{
	uintptr l, v, estack;
	extern char etext[];
	int i;

	iprint("dumpstack: cpu%d pid %lud pc %#p sp %#p\n", m->machno,
		up != nil? up->pid: 0, ureg->pc, ureg->sp);
	if(up != nil && (uintptr)&l >= (uintptr)up->kstack
	&& (uintptr)&l <= (uintptr)up->kstack+KSTACK)
		estack = (uintptr)up->kstack+KSTACK;
	else
		estack = ROUNDUP((uintptr)&l, 4096);
	i = 0;
	for(l = (uintptr)&l; l < estack; l += sizeof(uintptr)){
		v = *(uintptr*)l;
		if(v > KTZERO && v < (uintptr)etext){
			iprint("%#p ", v);
			if(++i == 6){
				i = 0;
				iprint("\n");
			}
		}
	}
	if(i)
		iprint("\n");
}

void
dumpstack(void)
{
	callwithureg(_dumpstack);
}

void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	ulong v;

	v = ureg->ie;
	memmove(pureg, uva, n);
	ureg->ie = v;
}

uintptr
userpc(void)
{
	Ureg *ur;

	ur = up->dbgreg;
	return ur->pc;
}

uintptr
dbgpc(Proc *p)
{
	Ureg *ur;

	ur = p->dbgreg;
	if(ur == nil)
		return 0;
	return ur->pc;
}

void
setkernur(Ureg *ureg, Proc *p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp;
	ureg->r1 = (uintptr)sched;
}

int
userureg(Ureg* ureg)
{
	switch (ureg->curmode) {
	default:
		return (ureg->status & Spp) == 0;
	case Mppmach:
		return (ureg->status & Mpp) == Mppuser;
	}
}

void
callwithureg(void (*f) (Ureg *))
{
	Ureg u;

	u.pc = getcallerpc(&f);
	u.sp = (uintptr)&f;
	f(&u);
}

static void
linkproc(void)
{
	spllo();
	up->kpfun(up->kparg);
	pexit("kproc exiting", 0);
}

/*
 *  setup stack and initial PC for a new kernel proc.
 */
void
kprocchild(Proc *p, void (*func)(void*), void *arg)
{
	p->sched.pc = PTR2UINT(linkproc);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK);
	p->kpfun = func;
	p->kparg = arg;
}

/*
 * called in syscallfmt.c, sysfile.c, sysproc.c.
 * as on the other ports, vlongs need only be 4-byte aligned.
 */
void
validalign(uintptr addr, unsigned align)
{
	if(align == sizeof(vlong))
		align = sizeof(long);
	if((align != 0 && !(align & (align-1))) && !(addr & (align-1)))
		return;
	postnote(up, 1, "sys: odd address", NDebug);
	error(Ebadarg);
}

typedef struct {
	/* exception or interrupt code from [ms]cause, without Rv64intr bit */
	uchar	cause;
	uchar	user;	/* flag: fault occurred while in user mode */
	uchar	type;	/* Faulttypes code */
	short	vno;	/* vector number; see vctlidx */
} Cause;
typedef int (*Traphandler)(Ureg *, Cause *);
typedef void (*Exchandler)(Ureg *, Cause *);

static char* excname[] = {
	"instruction address alignment",
	"instruction access",
	"illegal instruction",
	"breakpoint",
	"load address alignment",
	"load access",
	"store address alignment",
	"store access",
	"system call",
	"environment call from super mode",
	"#10 (reserved)",
	"environment call from machine mode",
	"instruction page fault",
	"load page fault",
	"#14 (reserved)",
	"store page fault",
	/* hypervisor stuff from here down */
/*
[Doubletrap]	"double trap",
[Swchk]		"software check",
[Hwerr]		"hardware error",
[Instgpage]	"instruction guest-page fault",
[Loadgpage]	"load guest-page fault",
[Virtinstr]	"virtual instruction",
[Storegpage]	"store guest-page fault",
*/
};

static void
trapdbg(Ureg *ureg, Cause *cp, int entry)
{
	extern int block;
	int type;

	type = cp->type;
	USED(type);
	/* if we print uart interrupts, we'll recurse forever. */
/*
	iprint("|%c%c%c ", ureg->curmode == Mppsuper? 'S': 'M', entry? '>': '<',
		type == Exception? 'E': 'I');
*/
	if (cp->cause >= nelem(excname) || excname[cp->cause] == nil)
		iprint("cause %d", cp->cause);
	else {
		iprint("%s", excname[cp->cause]);
		if (cp->cause == Envcalluser) {
			iprint(" pid %ld", up->pid);
			if (entry)
				iprint(" %llud", ureg->arg);
			else
				iprint(" return %#llux", ureg->arg);
		}
	}
	iprint(" from %s pc %#p tval %#p up %#p\n",
		cp->user? "user": "kernel", ureg->pc, ureg->tval, up);
}

static void
posttrapnote(Ureg *ureg, uint cause, char *name)
{
	char buf[ERRMAX];

	spllo();
	snprint(buf, sizeof buf, "sys: trap: %s for address %#p",
		cause < nelem(excname) && excname[cause]? excname[cause]: name,
		ureg->tval);
	postnote(up, 1, buf, NDebug);
}


/*
 * on riscv, we have to manually advance the PC past an ECALL instruction,
 * for example, so that we don't re-execute it.
 */
static void
advancepc(Ureg *ureg)
{
	/* examine short at PC, which is sufficient to decide if compressed */
	if ((ureg->pc & 1) == 0)
		ureg->pc += ISCOMPRESSED(*(ushort *)ureg->pc)? 2: 4;
}

static void
trapsyscall(Ureg *ureg, Cause *)
{
	syscall(ureg);
}

static void
trapcallmch(Ureg *, Cause *)
{
	panic("unexpected environment call from machine mode");
}

static void
trapmisaligned(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "misaligned access");
	else
		panic("misaligned access to %#p at %#p", ureg->tval, ureg->pc);
}

static void
debugbpt(Ureg* , Cause*)
{
	char buf[ERRMAX];

	if(up == 0)
		panic("kernel bpt");
	/*
	 * on riscv, pc points at the instruction that caused the trap,
	 * so there's no need to back up the pc.
	 */
	snprint(buf, sizeof buf, "sys: breakpoint");
	postnote(up, 1, buf, NDebug);
}


/* decode trap cause from *ureg into *cp & return type */
static int
whatcause(Cause *cp, Ureg *ureg)
{
	int type, cause;

	cp->user = userureg(ureg);
	if(cp->user){
		up->dbgreg = ureg;
		cycles(&up->kentry);
	}

	/* ureg->cause has MCAUSE or SCAUSE CSR, as appropriate */
	cause = ureg->cause & ~Rv64intr;
	if (!(ureg->cause & Rv64intr))
		type = Exception;
	else if ((cause & ~Msdiff) == Supextintr) {
		type = Globalintr;
		cause = 0;	/* actual causes will come from the plic */
	} else
		type = Localintr;	/* these are very frequent */
	cp->type = type;
	cp->cause = cause;

#ifdef xxx
	cp->vno = vctlidx(cause, type);
	if (cp->vno < 0)
		panic("trap: cause %d or type %d out of range", cause, type);

	/* these counts are reset every second */
	if (Trapdebug && cp->vno &&
	    ++trapcnt[cp->vno] % (1024*1024) == 0)	/* tweak to taste */
		iprint("trap: vector %d trapping lots\n", cp->vno);
#endif
	return type;
}

static void
trapaccess(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "illegal access");
	else
		panic("trap: illegal access to %#p at %#p", ureg->tval,
			ureg->pc);
}

static void
badinst(Ureg *ureg, Cause *cp)
{
	if (cp->user)
		posttrapnote(ureg, cp->cause, "illegal instruction");
	else
		panic("illegal instruction at %#p: %#p", ureg->pc, ureg->tval);
}

/* isfpinst from 9k */
static int
isfpinst(uintptr pc, Ureg *ureg)
{
	int n, csr;
	ulong inst;

	inst = *(ushort *)pc;		/* instrs are little-endian */
	if (ISCOMPRESSED(inst)) {
		n = (inst >> 13) & MASK(3);
		if  ((inst & 1) == 0 && (n == 1 || n == 5))
			return 1;	/* fp load/store double */
	} else {
		/* is it a regular instruction (possibly not 4-byte aligned)? */
		inst = UNCOMPINST(pc);
		if (inst != ureg->tval)
			iprint("illinst: inst %#lux tval %#llux\n",
				inst, ureg->tval);
		switch (BASEOP(inst) >> COMPBITS) {
		case 001:		/* load fp */
		case 011:		/* store fp */
		case 024:		/* fp op */
			return 1;
		case SYSTEM>>COMPBITS:
			/* check for fp csr instrs */
			if (((inst >> 12) & MASK(2)) != 0) {
				csr = inst >> 20;
				if (csr >= FFLAGS && csr <= FCSR)
					return 1;
			}
			break;
		}
	}
	return 0;
}
/*
 * Illinst could be from legitimate user fp use while fpu is off.
 * Bad CSR accesses will be noted and ignored.
 * It could also be due to the C910 erroneously trapping an unknown fence,
 * such as fence.tso.
 */
static void
trapillinst(Ureg *ureg, Cause *cp)
{
	int rd, skip, funct3;
	ulong inst;
	uintptr pc;

	/* if non-zero, ureg->tval will be the trapping instruction */
	pc = ureg->pc;
	if (pc & 1) {
		if (!cp->user)
			panic("trapillinst: odd pc %#p", pc);
		ureg->tval = pc;
		posttrapnote(ureg, cp->cause, "odd pc");
		return;
	}

	if (isfpinst(pc, ureg)) {
		mathtrap(ureg);
		return;			/* re-execute FP but with FPU on */
	}

	/*
	 * if we support vector extension, we'll need to cope with the
	 * vector instructions and vector processor here, analogously to fp.
	 * [ms]status.Vsst is vector context status.  ja and jl will have to
	 * implement the vector instructions first.
	 */

	if (ISCOMPRESSED(*(ushort *)pc)) {	/* instrs are little-endian */
		badinst(ureg, cp);
		return;
	}
	skip = 0;
	inst = UNCOMPINST(pc);
	if (inst != ureg->tval)
		iprint("illinst: inst %#lux tval %#llux\n", inst, ureg->tval);
	funct3 = (inst>>12) & MASK(3);
	switch (BASEOP(inst)) {
	case SYSTEM:
		if ((funct3 & MASK(2)) != 0) {
			/*
			 * bad CSR: zero dest and otherwise ignore.
			 * some CSRs are optional or obsolete.
			 * it could be a CSR that we want to emulate, in theory.
			 */
			iprint("CSR instruction for bad CSR %lx at %#p\n",
				inst>>20, pc);
			rd = (inst>>7) & MASK(5);
			if (rd)
				ureg->regs[rd] = 0;
			skip = 1;
		}
		break;
	case 0xf:
		if (funct3 == 0)
			/*
			 * some fence; shouldn't happen, but c910 is buggy.
			 * coherence has been called.
			 */
			skip = 1;
		break;
	}
	if (skip)
		ureg->pc += 4;
	else
		badinst(ureg, cp);
}

/*
 * the page fault couldn't be resolved, probably because the address is
 * unmapped.  Report the error in the appropriate way.
 */
static void
badpagefault(Ureg *ureg, uintptr addr, int read, int insyscall)
{
	char buf[ERRMAX];

	/*
	 * It is possible to get here with !user if, for example,
	 * a process was in a system call accessing a shared
	 * segment but was preempted by another process which shrunk
	 * or deallocated the shared segment; when the original
	 * process resumes it may fault while in kernel mode.
	 * No need to panic this case, post a note to the process
	 * and unwind the error stack. There must be an error stack
	 * (up->nerrlab != 0) if this is a system call, if not then
	 * the game's a bogey.
	 */
	if(!userureg(ureg) && (!insyscall || up->nerrlab == 0)){
		dumpregs(ureg);
		panic("fault: addr %#p pc %#p", addr, ureg->pc);
	}
	snprint(buf, sizeof buf, "sys: trap: fault %s addr=%#p",
		read? "read": "write", addr);
	postnote(up, 1, buf, NDebug);
	if(insyscall)
		error(buf);
}

/*
 *  find out fault address and type of access.
 *  Call common fault handler.
 */
static void
faultriscv64(Ureg* ureg, Cause *)
{
	uintptr addr;
	int read, insyscall;

	addr = ureg->tval;
	if(up == nil)
		panic("fault %#llud with up == nil; pc %#p addr %#p",
			ureg->cause, ureg->pc, addr);
	if(addr == 0 && up->nlocks.ref)
		panic("fault %#llud pc %#p addr %#p nlocks %ld",
			ureg->cause, ureg->pc, addr, up->nlocks.ref);

	insyscall = up->insyscall;
	up->insyscall = 1;
	read = ureg->cause != Storepage;  /* exception, so Rv64intr must be 0 */
	/* page fault on a kernel address is never okay. */
	if(addr < UZERO || fault(addr, read) < 0)
		badpagefault(ureg, addr, read, insyscall);
	up->insyscall = insyscall;
}

typedef struct Lastexcept Lastexcept;
struct Lastexcept {		/* should be per cpu */
	uintptr	pc;
	uintptr	addr;		/* failed address of load or store */
	short	consec;
};

static void
faultstuck(Ureg *ureg)
{
	static Lastexcept exc;

	if (ureg->pc == exc.pc && ureg->tval == exc.addr) {
		if (++exc.consec >= 10)
			panic("%d consecutive exceptions at pc %#p for addr %#p",
				exc.consec, exc.pc, exc.addr);
	} else {
		exc.pc = ureg->pc;
		exc.addr = ureg->tval;
		exc.consec = 0;
	}
}

/* see trap() for Envcalluser short cut, mtrap.s for Envcallsup */
static Exchandler exchandlers[] = {
[Breakpt]	debugbpt,
[Instpage]	faultriscv64,		/* page fault */
[Loadpage]	faultriscv64,
[Storepage]	faultriscv64,
[Instaccess]	trapaccess,		/* failed pmp or pma check */
[Loadaccess]	trapaccess,
[Storeaccess]	trapaccess,
[Illinst]	trapillinst,
[Instaddralign]	trapmisaligned,
[Loadaddralign]	trapmisaligned,
[Storeaddralign] trapmisaligned,
[Envcalluser]	trapsyscall,		/* backstop; short-cut in trap */
[Envcallmch]	trapcallmch,
/*
 * missing: Envcallvsup, Debugexc, Instgpage, Loadgpage, Virtinstr, Storegpage,
 * Doubletrap, Swchk, Hwerr
 */
};

/* handle exceptions */
static int
trapriscv64(Ureg *ureg, Cause *cp)
{
	uint cause;
	Exchandler handler;

	cause = cp->cause;
	if (cause >= nelem(exchandlers))
		panic("trapriscv64: cause %d out of range", cause);
	handler = exchandlers[cause];
	if (handler == nil)
		panic("unknown exception, cause %d from_user %d", cause, cp->user);
	(*handler)(ureg, cp);
	if (Trapdebug)
		faultstuck(ureg);
	return 0;			/* not a clock interrupt */
}

static int
traplocalintr(Ureg *ureg, Cause *cp)
{
	int clockintr;
	uint cause;

	clockintr = 0;
	m->intr++;			/* okay here; only tmr and sw intrs */
	m->perf.intrts = perfticks();
	cause = cp->cause;
	if (cause < Local0intr)
		cause &= ~Msdiff;	/* map mach to super codes */
	switch (cause) {
	case Suptmrintr:
		clockoff();
		if (++m->clockintrdepth > 1 && m->clockintrsok) {
			/* nested clock interrupt; probably shutting down */
			m->clockintrsok = 0;
		}
		timerintr(ureg, 0);
		--m->clockintrdepth;
		clockenable();
		clockintr = 1;
		break;
	case Supswintr:
		/*
		 * an ipi should normally pop out of wfi at splhi in idlehands,
		 * and not end up here.  getting here means that we sent an ipi
		 * to a cpu that stopped waiting before the ipi arrived, which
		 * is harmless.  sending the ipi zeroed ipiwait.  if we could
		 * interrupt idlehands, we would want to set mp->ipiwait=1 here
		 * so that it wouldn't be counted both by idlehands and below.
		 */
		gotipi = 1;
		clearipi();
		clockintr = 0;
		break;
	case Supextintr:
		/*
		 * NB: intr is not reached here, we short-circuited the cause
		 * to Globalintr in whatcause.
		 */
		panic("traplocalintr: handed an external interrupt");
	case 0:					/* probably NMI */
		if(m->machno == 0)
			panic("NMI @ %#p", ureg->pc);
		else {
			iprint("nmi: cpu%d: PC %#p\n", m->machno, ureg->pc);
			for(;;)
				idlehands();
		}
	default:
		panic("trap: unknown local interrupt %d", cp->cause);
	}
	/*
	 * STIP (sip.5) is asserted by hardware whenever mtime ≥ stimecmp,
	 * and csr writes to sip cannot clear it — the only way to deassert
	 * STIP is to advance stimecmp past mtime.  timerset() now does that
	 * unconditionally on its way out, but for non-timer local interrupts
	 * (sw IPI) timerset hasn't run, so STIP may still be pending here.
	 * If it is, advance stimecmp by one HZ tick so the trap can return
	 * without immediately re-firing on STIP; the real next deadline
	 * will be re-armed by timerintr the next time it runs.  This used
	 * to be an unconditional wrstimecmp(now + 0x100000) (43.7 ms at the
	 * K1's 24 MHz timebase), which clamped every periodic-timer cadence
	 * — including sub-HZ addclock0link() backstops — to ~43 ms.
	 */
	if ((vlong)(rdstimecmp() - rdtime()) <= 0)
		wrstimecmp(rdtime() + sys->clintsperhz);
	return clockintr;
}

/*
 * call for global interrupts (those coming from the plic).
 * these are never clock interrupts, so returns 0 (not a clockintr).
 */
int
intr(Ureg *ureg, Cause *cp)
{
	USED(cp);
	return irq(ureg);
}

static Traphandler traphandlers[Nfaulttypes] = {
[Unknownflt]	nil,
[Exception]	trapriscv64,			/* may enable fpu */
[Localintr]	traplocalintr,
[Globalintr]	intr,
};

/*
 *  All traps come here.  It is slower to have all traps call trap()
 *  rather than directly vectoring the handler.  However, this avoids a
 *  lot of code duplication and possible bugs.
 *  Trap is called with interrupts disabled.
 */
void
trap(Ureg* ureg)
{
	int clockintr;
	uint type;
	Cause why;
	Traphandler handler;

	type = whatcause(&why, ureg);
	if (Trapdebug)
		trapdbg(ureg, &why, 1);

	/* short-cut for syscalls; syscall() does the whole job */
	if (ureg->cause == Envcalluser) {	/* Rv64intr is off for exceptions */
		syscall(ureg);
		return;
	}

	if (type >= nelem(traphandlers))
		panic("trap: trap type %d too large", type);
	handler = traphandlers[type];
	if (handler == nil)
		panic("trap: trap type %d has no handler", type);
	clockintr = (*handler)(ureg, &why);
	splhi();		/* minimise harm if handler went low */

	/*
	 * delaysched set (because we held a lock or because our
	 * quantum ended)?
	 */
	if(up && up->delaysched && clockintr && m->clockintrsok) {
		sched();
		splhi();
	}
	if(why.user) {
		if(up->procctl || up->nnote)
			notify(ureg);
		/* the fpu state user mode returns with */
		fpsts2ureg(ureg);
	}
}

/*
 * Dump general registers.
 * try to fit it on a cga screen (80x25).
 */
static void
dumpgpr(Ureg* ureg)
{
	int i;

	if(up != nil)
		iprint("cpu%d: registers for %s %lud\n",
			m->machno, up->text, up->pid);
	else
		iprint("cpu%d: registers for kernel\n", m->machno);

	for (i = 1; i <= 31; i++)
		iprint("r%d\t%#16.16p%c", i, ureg->regs[i],
			i%2 == 0 || i == 31? '\n': '\t');
	iprint("pc\t%#p\t", ureg->pc);
	iprint("type\t%#p\t", ureg->type);
	iprint("m\t%#16.16p\nup\t%#16.16p\n", m, up);
}

void
dumpregs(Ureg* ureg)
{
	if(getconf("*nodumpregs")){
		iprint("dumpregs disabled\n");
		return;
	}
	dumpgpr(ureg);

	/*
	 * Processor CSRs.
	 */
	iprint("satp\t%#16.16llux\n", (uvlong)rsatp());
}


/* clear a software ipi delivered to this cpu (see idlewake/ipitohart) */
void
clearipi(void)
{
	if(nosbi || soc.ipiclint)
		m->clint->msip[m->hartid] = 0;
	else
		sbiclearipi();		/* legacy SBI: clears our SSIP */
	coherence();
	clrsipbit(Ssie);		/* deassert pending super sw intr */
}

/* base plic context for mach (M mode); all harts are equal on qemu virt */
uint
mach2context(Mach *)
{
	return 0;
}

