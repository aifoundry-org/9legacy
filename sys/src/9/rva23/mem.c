#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

void
meminit(void)
{
	char *p;
	uintptr l = VDRAM + 2ULL * GiB;
	extern u64int *sv57, *sv48, *sv39, *pGiB;

	if(p = getconf("*maxmem"))
		l = strtoull(p, 0, 0);

	/* leave a hole after end for the boot page tables */
	conf.mem[0].base = ROUND(PGROUND((uintptr)end - KTZERO) + (uintptr)(VDRAM+0x2fffff), 0x200000);
	conf.mem[0].limit = l;

	if(l > KLIMIT)
		l = KLIMIT;
	sv57 = (void *)PGROUND((uintptr)end);
	sv48 = (void *)PGROUND((uintptr)sv57 + 1);
	sv39 = (void *)PGROUND((uintptr)sv48 + 1);
	pGiB = (void *)PGROUND((uintptr)sv39 + 1);
	memset(sv57, 0, 16384);
	kmapram(conf.mem[0].base, l);

	conf.mem[0].npage = (conf.mem[0].limit - conf.mem[0].base)/BY2PG;
}
