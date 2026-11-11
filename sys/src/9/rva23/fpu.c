/*
 * floating point for user processes.  The kernel never uses the
 * fpu, so its registers belong to up whenever up->fpstate is
 * FPactive.  A process starts with the fpu off (FPinit); its first
 * fp instruction traps to mathtrap, which loads the registers.
 * procsave saves them and turns the fpu off again (FPinactive).
 *
 * SSTATUS.FS is restored from the Ureg on the way back to user
 * mode, so every return to user mode passes through fpsts2ureg.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "riscv64.h"

/* registers jc assumes hold 0, 0.5, 1 and 2 (F28-F31); see fp.s */
double fpzero = 0, fphalf = 0.5, fpone = 1, fptwo = 2;
static uvlong fpconst[4] = {
	0,
	0x3FE0000000000000ULL,
	0x3FF0000000000000ULL,
	0x4000000000000000ULL,
};

void
fpuinit(void)
{
	_fpuinit();
	fpoff();
}

static void
fpinitregs(FPsave *f)
{
	memset(f, 0, sizeof *f);
	memmove(&f->regs[28], fpconst, sizeof fpconst);
}

/* save up's live registers; leaves the fpu off */
void
fpusave(void)
{
	if(up->fpstate != FPactive)
		return;
	up->fpsave.fcsr = getfcsr();
	fpsaveregs(up->fpsave.regs);
	up->fpstate = FPinactive;
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
}

void
procsave(Proc *p)
{
	if(p->fpstate == FPactive){
		if(p->state == Moribund){
			fpoff();
			p->fpstate = FPinit;
		}else
			fpusave();
	}
	putasid(p);
}

void
procrestore(Proc*)
{
}

/*
 * the child of rfork inherits the parent's saved registers;
 * syscall saved them before the fork.
 */
void
fpuforkchild(Proc *p, Ureg *cureg)
{
	p->fpstate = up->fpstate;
	if(p->fpstate == FPactive)
		panic("fpuforkchild: parent fpu active");
	cureg->status &= ~Fsst;
}

/* the note handler runs with a copy of the interrupted fp state */
void
fpunotify(void)
{
	int s;

	s = splhi();
	fpusave();
	if(up->fpstate == FPinit){
		fpinitregs(&up->fpsave);
		up->fpstate = FPinactive;
	}
	up->notefpsave = up->fpsave;
	splx(s);
}

void
fpunoted(void)
{
	int s;

	s = splhi();
	if(up->fpstate == FPactive)
		fpoff();
	up->fpsave = up->notefpsave;
	up->fpstate = FPinactive;
	splx(s);
}

/* user fp instruction with the fpu off: load up's registers */
void
mathtrap(Ureg *ureg)
{
	if(up == nil || !userureg(ureg))
		panic("fp instruction in kernel at %#p", ureg->pc);
	switch(up->fpstate){
	case FPinit:
		fpinitregs(&up->fpsave);
		/* fall through */
	case FPinactive:
		fploadregs(up->fpsave.regs);
		setfcsr(up->fpsave.fcsr);
		up->fpstate = FPactive;
		break;
	case FPactive:
		postnote(up, 1, "sys: trap: illegal fp instruction", NDebug);
		break;
	}
}

/* set the fpu state user mode will return with */
void
fpsts2ureg(Ureg *ureg)
{
	ureg->status &= ~Fsst;
	if(up->fpstate == FPactive)
		ureg->status |= fpstatus();
}

long
clrfpintr(void)
{
	return 0;
}
