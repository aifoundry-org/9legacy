/*
 * #P/cputype, shutdown and timing.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "riscv64.h"

#include <ctype.h>

enum {
	Qdir = 0,
	Qbase,

	Qmax = 16,
};

typedef long Rdwrfn(Chan*, void*, long, vlong);
static void archinit(void);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static Dirtab archdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	0555,
};
Lock archwlock;	/* the lock is only for changing archdir */
int narchdir = Qbase;

extern Dev archdevtab;

/*
 * Add a file to the #P listing.  Once added, you can't delete it.
 * You can't add a file with the same name as one already there,
 * and you get a pointer to the Dirtab entry so you can do things
 * like change the Qid version.  Changing the Qid path is disallowed.
 */
Dirtab*
addarchfile(char *name, int perm, Rdwrfn *rdfn, Rdwrfn *wrfn)
{
	int i;
	Dirtab d;
	Dirtab *dp;

	memset(&d, 0, sizeof d);
	strcpy(d.name, name);
	d.perm = perm;

	lock(&archwlock);
	if(narchdir >= Qmax){
		unlock(&archwlock);
		return nil;
	}

	for(i=0; i<narchdir; i++)
		if(strcmp(archdir[i].name, name) == 0){
			unlock(&archwlock);
			return nil;
		}

	d.qid.path = narchdir;
	archdir[narchdir] = d;
	readfn[narchdir] = rdfn;
	writefn[narchdir] = wrfn;
	dp = &archdir[narchdir++];
	unlock(&archwlock);
	return dp;
}

static Chan*
archattach(char* spec)
{
	return devattach(archdevtab.dc, spec);
}

Walkqid*
archwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int
archstat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static Chan*
archopen(Chan* c, int omode)
{
	return devopen(c, omode, archdir, narchdir, devgen);
}

static void
archclose(Chan*)
{
}

enum
{
	Linelen= 31,
};

static long
archread(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){

	case Qdir:
		return devdirread(c, a, n, archdir, narchdir, devgen);
	default:
		if(c->qid.path < narchdir && (fn = readfn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
	}
	return 0;
}

static long
archwrite(Chan *c, void *a, long n, vlong offset)
{
	Rdwrfn *fn;

	switch((ulong)c->qid.path){
	default:
		if(c->qid.path < narchdir && (fn = writefn[c->qid.path]))
			return fn(c, a, n, offset);
		error(Eperm);
	}
	return 0;
}

Dev archdevtab = {
	'P',
	"arch",

	devreset,
	archinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

void
nop(void)				/* unused */
{
}

vlong
vlabs(vlong vl)				/* unused */
{
	return vl >= 0? vl: -vl;
}

static char vendor[19], arch[19], impid[19];
static char cpuid[sizeof(vendor)+sizeof(arch)+sizeof(impid)];
static char cputype[150];

static long
cputyperead(Chan*, void *a, long n, vlong off)
{
	return readstr(off, a, n, cputype);
}

static long
cpuidread(Chan*, void *a, long n, vlong off)
{
	return readstr(off, a, n, cpuid);
}

static ulong exts[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 0x10,
	HSM,
	's'<<16|'P'<<8|'I',		/* sPI */
	'R'<<24|'F'<<16|'N'<<8|'C',	/* RFNC */
	SRST,
};

static void
sbiext(ulong ext)
{
	char c;

	if (sbiprobeext(ext) == 0)
		return;
	if (ext < 256)
		print(" %#lux", ext);
	else {
		print(" ");
		for (; ext > 0; ext <<= 8) {
			c = ext >> 24;
			if (c && isprint(c))
				print("%c", c);
		}
	}
}

static void
sbiinit(void)
{
	int i;

	if (!bootmachmode && !nosbi) {
		soc.havesbihsm  = sbiprobeext(HSM) != 0;
		soc.havesbisrst = sbiprobeext(SRST) != 0;
		if (!soc.newmach)
			return;
		print("sbi extensions present:");
		for (i = 0; i < nelem(exts); i++)
			sbiext(exts[i]);
		print("\n");
	}
}

static long
pmctlwrite(Chan*, void *a, long n, vlong)
{
	Cmdbuf *cb;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(cb->nf >= 2 && strcmp(cb->f[0], "power") == 0
	   && strcmp(cb->f[1], "off") == 0){
		print("powering off\n");
		poweroff();
	}
	error(Ebadctl);
	return 0;
}

static void
archinit(void)
{
	snprint(vendor, sizeof(vendor), "0x%016ullx", sbigetmvendorid());
	snprint(arch, sizeof(arch), "0x%016ullx", sbigetmarchid());
	snprint(impid, sizeof(impid), "0x%016ullx", sbigetmimpid());
	snprint(cpuid, sizeof(cpuid), "%s %s %s", vendor, arch, impid);
	snprint(cputype, sizeof(cputype), "riscv64 %ud\n", m->cpumhz);
	addarchfile("cputype", 0444, cputyperead, nil);
	addarchfile("cpuid", 0444, cpuidread, nil);
	addarchfile("pmctl", 0220, nil, pmctlwrite);
	sbiinit();
}

/*
 * attempt to reset the whole system.
 * failing that, just hang.
 */
void
archreset(void)
{
	iprint("archreset...");
	spllo();
	delay(20);
	if (!nosbi && m->machno == 0) {
		if (soc.havesbisrst) {
			iprint("sbi cold reboot...");
			delay(20);
			sbireset(SRSTCOLD, 0);
			sbireset(SRSTWARM, 0);
			/* shouldn't get here */
		}
		iprint("trying sbi shutdown.\n");
		delay(20);
		sbishutdown();			/* shuts down all cpus */
	}
	iprint("last resort for reset: into wfi.\n");
	delay(20);
	for (;;)
		wfi();
}

