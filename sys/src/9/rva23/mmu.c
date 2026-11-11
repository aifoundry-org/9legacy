#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/* the kernel identity-maps everything below user space */
uintptr klimit = UZERO;

uintptr
paddr(void *va)
{
	if((uintptr)va < KLIMIT)
		return (uintptr)va;
	panic("paddr: va=%#p pc=%#p", va, getcallerpc(&va));
	return 0;
}

uintptr
cankaddr(uintptr pa)
{
	if(pa < (uintptr)KLIMIT)
		return KLIMIT - pa;
	return 0;
}

void*
kaddr(uintptr pa)
{
	if(pa < (uintptr)KLIMIT)
		return (void*)(pa);
	panic("kaddr: pa=%#p pc=%#p", pa, getcallerpc(&pa));
	return nil;
}

static void*
kmapaddr(uintptr pa)
{
	return kaddr(pa);
}

KMap*
kmap(Page *p)
{
	return kmapaddr(p->pa);
}

void
kunmap(KMap*)
{
}

void
kmapinval(void)
{
}

u64int *sv57, *sv48, *sv39, *pGiB;
u64int mmumode;

/*
 * Carve one page-table page out of the front of the physical pool at boot,
 * before conf.npage is known.  Nothing has zeroed the pool, so zero the
 * page here.  Identity mapped, so va == pa.
 */
u64int*
bootptpage(void)
{
	uintptr pa;

	pa = conf.mem[0].base;
	assert((pa & (BY2PG-1)) == 0);
	conf.mem[0].base += BY2PG;
	memset((void*)pa, 0, BY2PG);
	return (u64int*)pa;
}

void
vunmap(void *, vlong)
{
}

static int
vpn(uintptr va, int level)
{
	return (va>>(12 + 9*level)) & 0x1ff;
}

/*
 * walk up's page tables, rooted in m->mmutop (see mmuswitch),
 * to the pte for va at level, adding tables from up->mmufree.
 */
static uintptr*
mmuwalk(uintptr va, int level)
{
	uintptr *table, pte;
	Page *pg;
	int i, x;

	x = vpn(va, PTLEVELS-1);
	table = m->mmutop;
	for(i = PTLEVELS-2; i >= level; i--){
		pte = table[x];
		if(pte & PTEVALID)
			pte = PTE2PA(pte);
		else {
			pg = up->mmufree;
			if(pg == nil)
				return nil;
			up->mmufree = pg->next;
			pg->va = va & -PGLSZ(i+1);
			if((pg->next = up->mmuhead[i+1]) == nil)
				up->mmutail[i+1] = pg;
			up->mmuhead[i+1] = pg;
			pte = pg->pa;
			memset(kmapaddr(pg->pa), 0, BY2PG);
			coherence();
			table[x] = PA2PTE(pte) | PTEVALID;
		}
		table = kmapaddr(pte);
		x = vpn(va, (uintptr)i);
	}
	return &table[x];
}

static Proc *asidlist[256];

static int
allocasid(Proc *p)
{
	static Lock lk;
	Proc *x;
	int a;

	lock(&lk);
	a = p->asid;
	if(a < 0)
		a = -a;
	if(a == 0)
		a = p->pid;
	for(;; a++){
		a %= nelem(asidlist);
		if(a == 0)
			continue;	/* reserved */
		x = asidlist[a];
		if(x == p || x == nil || (x->asid < 0 && x->mach == nil))
			break;
	}
	p->asid = a;
	asidlist[a] = p;
	unlock(&lk);

	return x != p;
}

static void
freeasid(Proc *p)
{
	int a;

	a = p->asid;
	if(a < 0)
		a = -a;
	if(a > 0 && asidlist[a] == p)
		asidlist[a] = nil;
	p->asid = 0;
}

void
putasid(Proc *p)
{
	/*
	 * Prevent the following scenario:
	 *	pX sleeps on cpuA, leaving its page tables in mmutop
	 *	pX wakes up on cpuB, and exits, freeing its page tables
	 *  pY on cpuB allocates a freed page table page and overwrites with data
	 *  cpuA takes an interrupt, and is now running with bad page tables
	 * In theory this shouldn't hurt because only user address space tables
	 * are affected, and mmuswitch will clear mmutop before a user process is
	 * dispatched.  But empirically it correlates with weird problems, eg
	 * resetting of the core clock at 0x4000001C which confuses local timers.
	 */
	if(conf.nmach > 1)
		mmuswitch(nil);

	if(p->asid > 0)
		p->asid = -p->asid;
}

/*
 * pa carries the access bits from fault.c (PTEVALID, PTERONLY or
 * PTEWRITE) in its low bits and possibly PTEUNCACHED in the Svpbmt
 * field; a user page is always readable and executable.
 */
void
putmmu(uintptr va, uintptr pa, Page *pg)
{
	uintptr *pte, old, attr, pbmt;
	int s;

	attr = PTE2ATTR(pa) | PTEREAD | PTEUSER | PTEACCESSED | PTEEXEC;
	if(attr & PTEWRITE)
		attr |= PTEDIRTY;
	pbmt = 0;
	if(soc.svpbmt)
		pbmt = pa & PTEPBMT;
	pa = PPN(pa & ~PTEPBMT);

	s = splhi();
	while((pte = mmuwalk(va, 0)) == nil){
		spllo();
		up->mmufree = newpage(0, 0, 0);
		splhi();
	}
	old = *pte;
	*pte = 0;
	if((old & PTEVALID) != 0)
		flushasidvall((uvlong)up->asid<<48 | va>>12);
	else
		flushasidva((uvlong)up->asid<<48 | va>>12);
	*pte = PA2PTE(pa) | attr | pbmt;
	if(pg->cachectl[m->machno] == PG_TXTFLUSH){
		/* pio() sets PG_TXTFLUSH whenever a text page has been written */
		cachedwbinvse(kmap(pg), BY2PG);
		cacheiinvse((void*)va, BY2PG);
		pg->cachectl[m->machno] = PG_NOFLUSH;
	}
	flushalltlb();
	splx(s);
}

static void
mmufree(Proc *p)
{
	int i;

	freeasid(p);

	for(i=1; i<PTLEVELS; i++){
		if(p->mmuhead[i] == nil)
			break;
		p->mmutail[i]->next = p->mmufree;
		p->mmufree = p->mmuhead[i];
		p->mmuhead[i] = p->mmutail[i] = nil;
	}
}

/*
 * load p's top-level user entries into this hart's root table;
 * p->newtlb asks for its page tables to be discarded first.
 */
void
mmuswitch(Proc *p)
{
	uintptr va;
	Page *t;

	for(va = UZERO; va < USTKTOP; va += PGLSZ(PTLEVELS-1))
		m->mmutop[PTLX(va, PTLEVELS-1)] = 0;

	if(p == nil){
		wsatp(((uintptr)m->mmutop>>12)|(8ULL<<60));
		return;
	}

	if(p->newtlb){
		mmufree(p);
		p->newtlb = 0;
	}
	if(allocasid(p))
		flushasid((uvlong)p->asid<<48);

	for(t = p->mmuhead[PTLEVELS-1]; t != nil; t = t->next){
		va = t->va;
		m->mmutop[PTLX(va, PTLEVELS-1)] = PA2PTE(t->pa) | PTEVALID;
	}
	wsatp(((uintptr)m->mmutop>>12)|(8ULL<<60));
}

void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

void
mmurelease(Proc *p)
{
	Page *pg, *next;

	mmuswitch(nil);
	mmufree(p);
	for(pg = p->mmufree; pg != nil; pg = next){
		next = pg->next;
		if(--pg->ref)
			panic("mmurelease: pg->ref %d", pg->ref);
		pagechainhead(pg);	/* sched holds palloc */
	}
	if(p->mmufree != nil && palloc.r.p != nil)
		wakeup(&palloc.r);
	p->mmufree = nil;
}

void
checkmmu(uintptr, uintptr)
{
}
