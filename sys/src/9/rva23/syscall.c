/*
 * system calls, notes, fork and exec for riscv64.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/systab.h"

#include <tos.h>
#include "ureg.h"
#include "riscv64.h"

typedef struct {
	uintptr	ip;
	Ureg*	arg0;
	char*	arg1;
	char	msg[ERRMAX];
	Ureg*	old;
	Ureg	ureg;
} NFrame;

/*
 * jc passes int and long arguments in 4-byte stack slots and
 * pointers and vlongs in 8-byte aligned ones, so the layout of
 * the arguments above the user's sp depends on the call.
 * i: 4-byte integer, p: pointer or uintptr, v: vlong.
 */
static char *sysargs[] = {
[SYSR1]		"",
[_ERRSTR]	"p",
[BIND]		"ppi",
[CHDIR]		"p",
[CLOSE]		"i",
[DUP]		"ii",
[ALARM]		"i",
[EXEC]		"pp",
[EXITS]		"p",
[_FSESSION]	"ipi",
[FAUTH]		"ip",
[_FSTAT]	"ip",
[SEGBRK]	"pp",
[_MOUNT]	"ipip",
[OPEN]		"pi",
[_READ]		"ipi",
[OSEEK]		"iii",
[SLEEP]		"i",
[_STAT]		"pp",
[RFORK]		"i",
[_WRITE]	"ipi",
[PIPE]		"p",
[CREATE]	"pii",
[FD2PATH]	"ipi",
[BRK_]		"p",
[REMOVE]	"p",
[_WSTAT]	"pp",
[_FWSTAT]	"ip",
[NOTIFY]	"p",
[NOTED]		"i",
[SEGATTACH]	"ippi",
[SEGDETACH]	"p",
[SEGFREE]	"pi",
[SEGFLUSH]	"pi",
[RENDEZVOUS]	"pp",
[UNMOUNT]	"pp",
[_WAIT]		"p",
[SEMACQUIRE]	"pi",
[SEMRELEASE]	"pi",
[SEEK]		"pivi",
[FVERSION]	"iipi",
[ERRSTR]	"pi",
[STAT]		"ppi",
[FSTAT]		"ipi",
[WSTAT]		"ppi",
[FWSTAT]	"ipi",
[MOUNT]		"iipip",
[AWAIT]		"pi",
[PREAD]		"ipiv",
[PWRITE]	"ipiv",
[TSEMACQUIRE]	"pi",
[NSEC]		"",
};

/* the arguments start just above the return pc slot at sp */
static void
getargs(int scallnr, uintptr sp)
{
	char *s;
	uchar *a;
	int i, off;

	if(sp < USTKTOP-BY2PG || sp > USTKTOP-sizeof(Sargs)-BY2WD)
		validaddr(sp, sizeof(Sargs)+BY2WD, 0);
	a = (uchar*)(sp+BY2WD);
	memset(&up->s, 0, sizeof up->s);
	s = nil;
	if(scallnr >= 0 && scallnr < nelem(sysargs))
		s = sysargs[scallnr];
	if(s == nil){
		/* unknown: pass 8-byte slots */
		up->s = *(Sargs*)a;
		return;
	}
	off = 0;
	for(i = 0; s[i] != 0 && i < MAXSYSARG; i++){
		if(s[i] == 'i'){
			off = (off+3) & ~3;
			up->s.args[i] = *(u32int*)(a+off);
			off += 4;
		}else{
			off = (off+7) & ~7;
			up->s.args[i] = *(uintptr*)(a+off);
			off += 8;
		}
	}

	/* sseek wants the offset as two words */
	if(scallnr == SEEK){
		up->s.args[4] = up->s.args[3];
		up->s.args[3] = up->s.args[2]>>32;
		up->s.args[2] &= 0xFFFFFFFFULL;
	}
}

/*
 *   Return user to state before notify()
 */
static void
noted(Ureg* cur, uintptr arg0)
{
	NFrame *nf;
	Ureg *nur;

	qlock(&up->debug);
	if(arg0 != NRSTR && !up->notified){
		qunlock(&up->debug);
		pprint("call to noted() when not notified\n");
		pexit("Suicide", 0);
	}
	up->notified = 0;
	fpunoted();

	nf = up->ureg;

	/* sanity clause */
	if(!okaddr(PTR2UINT(nf), sizeof(NFrame), 0)){
		qunlock(&up->debug);
		pprint("bad ureg in noted %#p\n", nf);
		pexit("Suicide", 0);
	}

	/* don't let user change system flags */
	nur = &nf->ureg;
	nur->status = cur->status;
	nur->ie = cur->ie;
	nur->curmode = cur->curmode;
	memmove(cur, nur, sizeof(Ureg));

	switch((int)arg0){
	case NCONT:
	case NRSTR:
		if(!okaddr(cur->pc, BY2WD, 0) || !okaddr(cur->sp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		up->ureg = nf->old;
		qunlock(&up->debug);
		break;
	case NSAVE:
		if(!okaddr(cur->pc, BY2WD, 0) || !okaddr(cur->sp, BY2WD, 0)){
			qunlock(&up->debug);
			pprint("suicide: trap in noted\n");
			pexit("Suicide", 0);
		}
		qunlock(&up->debug);

		splhi();
		nf->arg1 = nf->msg;
		nf->arg0 = &nf->ureg;
		nf->ip = 0;
		cur->sp = PTR2UINT(nf);
		cur->arg = PTR2UINT(nf->arg0);
		break;
	default:
		pprint("unknown noted arg %#p\n", arg0);
		up->lastnote.flag = NDebug;
		/* fall through */
	case NDFLT:
		if(up->lastnote.flag == NDebug){
			qunlock(&up->debug);
			pprint("suicide: %s\n", up->lastnote.msg);
		}else
			qunlock(&up->debug);
		pexit(up->lastnote.msg, up->lastnote.flag != NDebug);
	}
}

/*
 *  Call user, if necessary, with note.
 *  Pass user the Ureg struct and the note on his stack.
 */
int
notify(Ureg* ureg)
{
	int l, s;
	Note *n;
	uintptr sp;
	NFrame *nf;

	if(up->procctl)
		procctl(up);
	if(up->nnote == 0)
		return 0;

	fpunotify();

	s = spllo();
	qlock(&up->debug);

	up->notepending = 0;
	n = &up->note[0];
	if(strncmp(n->msg, "sys:", 4) == 0){
		l = strlen(n->msg);
		if(l > ERRMAX-23)	/* " pc=0x0123456789abcdef\0" */
			l = ERRMAX-23;
		snprint(n->msg + l, sizeof n->msg - l, " pc=%#p", ureg->pc);
	}

	if(n->flag != NUser && (up->notified || up->notify == 0)){
		if(n->flag == NDebug)
			pprint("suicide: %s\n", n->msg);
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}

	if(up->notified){
		qunlock(&up->debug);
		splhi();
		return 0;
	}

	if(up->notify == nil){
		qunlock(&up->debug);
		pexit(n->msg, n->flag != NDebug);
	}
	if(!okaddr(PTR2UINT(up->notify), 1, 0)){
		pprint("suicide: notify function address %#p\n", up->notify);
		qunlock(&up->debug);
		pexit("Suicide", 0);
	}

	sp = ureg->sp - sizeof(NFrame);
	sp &= ~(uintptr)15;
	if(!okaddr(sp, sizeof(NFrame), 1)){
		qunlock(&up->debug);
		pprint("suicide: notify stack address %#p\n", sp);
		pexit("Suicide", 0);
	}

	nf = UINT2PTR(sp);
	memmove(&nf->ureg, ureg, sizeof(Ureg));
	nf->old = up->ureg;
	up->ureg = nf;
	memmove(nf->msg, up->note[0].msg, ERRMAX);
	nf->arg1 = nf->msg;
	nf->arg0 = &nf->ureg;
	ureg->arg = PTR2UINT(nf->arg0);
	nf->ip = 0;
	ureg->sp = sp;
	ureg->pc = PTR2UINT(up->notify);
	up->notified = 1;
	up->nnote--;
	memmove(&up->lastnote, &up->note[0], sizeof(Note));
	memmove(&up->note[0], &up->note[1], up->nnote*sizeof(Note));

	qunlock(&up->debug);
	splx(s);

	return 1;
}

/* called from trap with the ECALL still at ureg->pc */
void
syscall(Ureg* ureg)
{
	char *e;
	int s;
	uintptr ret;
	int i, scallnr;
	vlong startns, stopns;

	if(!userureg(ureg))
		panic("syscall: from kernel: pc %#p", ureg->pc);

	cycles(&up->kentry);

	m->syscall++;
	up->insyscall = 1;
	ureg->pc += 4;		/* ECALL has no compressed form */
	up->pc = ureg->pc;
	up->dbgreg = ureg;

	scallnr = ureg->arg;
	up->scallnr = scallnr;
	if(scallnr == RFORK)
		fpusave();
	spllo();

	getargs(scallnr, ureg->sp);

	if(up->procctl == Proc_tracesyscall){
		syscallfmt(scallnr, ureg->pc, (va_list)(up->s.args));
		up->procctl = Proc_stopme;
		procctl(up);
		if(up->syscalltrace)
			free(up->syscalltrace);
		up->syscalltrace = nil;
	}

	up->nerrlab = 0;
	ret = -1;
	startns = todget(nil, nil);
	if(!waserror()){
		if(scallnr >= nsyscall || scallnr < 0){
			pprint("bad sys call number %d pc %#p\n",
				scallnr, ureg->pc);
			postnote(up, 1, "sys: bad sys call", NDebug);
			error(Ebadarg);
		}

		up->psstate = sysctab[scallnr];

		/* libc's nsec expects the time as the return value */
		if(scallnr == NSEC)
			ret = startns;
		else
			ret = systab[scallnr](up->s.args);
		poperror();
	}else{
		/* failure: save the error buffer for errstr */
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
	}
	if(up->nerrlab){
		print("bad errstack [%d]: %d extra\n", scallnr, up->nerrlab);
		for(i = 0; i < NERR; i++)
			print("sp=%#p pc=%#p\n",
				up->errlab[i].sp, up->errlab[i].pc);
		panic("error stack");
	}

	ureg->ret = ret;

	if(up->procctl == Proc_tracesyscall){
		stopns = todget(nil, nil);
		up->procctl = Proc_stopme;
		sysretfmt(scallnr, (va_list)(up->s.args), ret, startns, stopns);
		s = splhi();
		procctl(up);
		splx(s);
		if(up->syscalltrace)
			free(up->syscalltrace);
		up->syscalltrace = nil;
	}

	up->insyscall = 0;
	up->psstate = 0;

	if(scallnr == NOTED)
		noted(ureg, up->s.args[0]);

	splhi();
	if(scallnr != RFORK && (up->procctl || up->nnote))
		notify(ureg);

	/* if we delayed sched because we held a lock, sched now */
	if(up->delaysched){
		sched();
		splhi();
	}
	kexit(ureg);
}

/* update the user's Tos on the way out of the kernel */
void
kexit(Ureg *ureg)
{
	uvlong t;
	Tos *tos;

	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = up->pcycles;
	tos->cyclefreq = m->cyclefreq;
	tos->pid = up->pid;
	if(ureg != nil)
		fpsts2ureg(ureg);
}

uintptr
execregs(uintptr entry, ulong ssize, ulong nargs)
{
	uintptr sp;
	Ureg *ureg;

	ureg = up->dbgreg;
	sp = (uintptr)(USTKTOP - ssize);
	sp -= sizeof(uintptr);
	*(uintptr*)sp = nargs;
	ureg->sp = sp;
	ureg->pc = entry;

	/*
	 * return the address of kernel/user shared data
	 * (e.g. clock stuff); main9.s takes it from R8.
	 */
	return USTKTOP-sizeof(Tos);
}

/*
 * the child bypasses the syscall exit path on its first return to
 * user mode, so stamp its Tos here: libc's malloc locking compares
 * _tos->pid and would see the parent's.
 */
void
forkret(void)
{
	kexit(nil);
	sysrforkret();
}

/*
 *  Craft a return frame which will cause the child to pop out of
 *  the scheduler in user mode with the return register zero.
 *  strap.s finds the Ureg at the top of the kernel stack.
 */
void
forkchild(Proc *p, Ureg *ureg)
{
	Ureg *cureg;

	p->sched.sp = (uintptr)p->kstack+KSTACK-sizeof(Ureg);
	p->sched.pc = (uintptr)forkret;

	cureg = (Ureg*)(p->sched.sp);
	memmove(cureg, ureg, sizeof(Ureg));

	/* syscall returns 0 for child */
	cureg->ret = 0;

	/* Things from bottom of syscall which were never executed */
	p->psstate = 0;
	p->insyscall = 0;

	fpuforkchild(p, cureg);
}
