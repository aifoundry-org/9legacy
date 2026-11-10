#include <u.h>
#include <libc.h>

/*
 * ktest checks the parts of a kernel a port gets wrong:
 * system call argument passing, notes and the floating point state
 * around them, 64-bit file offsets, nsec, brk and stack growth,
 * semaphores, fork, wait and faults.  Each check prints a line;
 * any failure exits non-empty.
 */

enum
{
	Nbrk	= 64*1024*1024,		/* bytes malloc'd and touched */
	Ndeep	= 4000,			/* recursion depth, 1K frames */
};

int	nnote;
double	fpnote;
long	sem;		/* not on the stack: RFMEM does not share it */

void
fail(char *fmt, ...)
{
	va_list a;
	char buf[ERRMAX];

	va_start(a, fmt);
	vseprint(buf, buf+sizeof buf, fmt, a);
	va_end(a);
	fprint(2, "ktest: %s\n", buf);
	exits("fail");
}

void
ok(char *what)
{
	print("ktest: %s ok\n", what);
}

int
handler(void*, char *msg)
{
	double x;

	if(strcmp(msg, "alarm") == 0 || strncmp(msg, "ktest", 5) == 0){
		x = 1.25;
		fpnote += x*2;		/* dirty the fp registers */
		nnote++;
		return 1;
	}
	return 0;
}

/* alarm and /proc notes, with fp state live across them */
void
notes(void)
{
	int fd;
	double d;
	char buf[64];

	d = 3.0;
	alarm(200);
	sleep(500);
	if(nnote != 1 || fpnote != 2.5)
		fail("alarm: %d notes, fp %g", nnote, fpnote);
	snprint(buf, sizeof buf, "/proc/%d/note", getpid());
	if((fd = open(buf, OWRITE)) < 0)
		fail("open %s: %r", buf);
	if(write(fd, "ktest", 5) != 5)
		fail("write note: %r");
	close(fd);
	sleep(10);
	if(nnote != 2 || fpnote != 5.0)
		fail("proc note: %d notes, fp %g", nnote, fpnote);
	if(d*1.5 != 4.5)
		fail("fp register clobbered by notes: %g", d*1.5);
	ok("notes");
}

/* vlong arguments and results: seek, pread and pwrite past 4G */
void
offsets(void)
{
	int fd;
	vlong off;
	char *f, buf[8];

	f = "/tmp/ktest.big";
	if((fd = create(f, ORDWR, 0666)) < 0)
		fail("create %s: %r", f);
	off = 5LL*1024*1024*1024 + 123;
	if(pwrite(fd, "hello", 5, off) != 5)
		fail("pwrite: %r");
	if(seek(fd, 0, 2) != off+5)
		fail("seek end");
	memset(buf, 0, sizeof buf);
	if(pread(fd, buf, 5, off) != 5 || strcmp(buf, "hello") != 0)
		fail("pread: %r");
	if(seek(fd, off-23, 0) != off-23)
		fail("seek set");
	close(fd);
	remove(f);
	ok("offsets");
}

void
clock(void)
{
	vlong t0, t1;

	t0 = nsec();
	sleep(100);
	t1 = nsec();
	if(t1-t0 < 90*1000000LL || t1-t0 > 5000*1000000LL)
		fail("nsec: slept 100ms, took %lldns", t1-t0);
	ok("nsec");
}

int
deep(int n)
{
	char b[1024];

	memset(b, n, sizeof b);
	if(n == 0)
		return 0;
	return deep(n-1) + b[10] - b[11] + 1;
}

void
memory(void)
{
	uchar *p;
	int i;

	if((p = malloc(Nbrk)) == nil)
		fail("malloc %d: %r", Nbrk);
	for(i = 0; i < Nbrk; i += 4096)
		p[i] = i>>12;
	for(i = 0; i < Nbrk; i += 4096)
		if(p[i] != ((i>>12) & 0xFF))
			fail("page %d corrupt", i>>12);
	free(p);
	if(deep(Ndeep) != Ndeep)
		fail("stack growth");
	ok("memory");
}

void
sems(void)
{
	int pid;

	sem = 0;
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		fail("rfork: %r");
	case 0:
		sleep(100);
		semrelease(&sem, 1);
		_exits(nil);
	}
	if(semacquire(&sem, 1) != 1)
		fail("semacquire: %r");
	if(tsemacquire(&sem, 50) != 0)
		fail("tsemacquire did not time out");
	while(waitpid() != pid)
		;
	ok("semaphores");
}

void
procs(void)
{
	int p[2], pid, n;
	char buf[64], want[64];
	Waitmsg *w;

	if(pipe(p) < 0)
		fail("pipe: %r");
	switch(pid = fork()){
	case -1:
		fail("fork: %r");
	case 0:
		close(p[0]);
		fprint(p[1], "child %d", getpid());
		exits("child status");
	}
	close(p[1]);
	n = read(p[0], buf, sizeof buf-1);
	buf[n > 0? n: 0] = 0;
	close(p[0]);
	snprint(want, sizeof want, "child %d", pid);
	if(strcmp(buf, want) != 0)
		fail("pipe read %q", buf);
	if((w = wait()) == nil || w->pid != pid || strstr(w->msg, "child status") == nil)
		fail("wait: %r");
	free(w);

	switch(fork()){
	case -1:
		fail("fork: %r");
	case 0:
		*(int*)8 = 1;
		exits(nil);
	}
	if((w = wait()) == nil || strstr(w->msg, "fault write addr=0x8") == nil)
		fail("fault child: %s", w? w->msg: "no wait");
	free(w);
	ok("processes");
}

void
main(void)
{
	atnotify(handler, 1);
	notes();
	offsets();
	clock();
	memory();
	sems();
	procs();
	print("ktest: all ok\n");
	exits(nil);
}
