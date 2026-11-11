/*
 * Comparisons and truth tests of values converted to
 * char, uchar, short and ushort (and from vlong to int).
 * Prints one line of results per test; compare the output
 * with that of another compiler (e.g. 8c or 6c).
 */
#include <u.h>
#include <libc.h>

char	gc;
uchar	guc;
short	gs;
ushort	gus;
int	gi;

int	vals[] = {
	0, 1, -1, 127, 128, -128, -129, 255, 256, 384,
	32767, 32768, -32768, -32769, 65535, 65536,
	0x7fffffff, 0x80000000, 0x12345680,
};

#define	REL(x, y)\
	switch(op){\
	case 0:	return x == y;\
	case 1:	return x != y;\
	case 2:	return x < y;\
	case 3:	return x <= y;\
	case 4:	return x > y;\
	case 5:	return x >= y;\
	}\
	return -1;

#define	BREL(x, y)\
	switch(op){\
	case 0:	if(x == y) return 1; break;\
	case 1:	if(x != y) return 1; break;\
	case 2:	if(x < y) return 1; break;\
	case 3:	if(x <= y) return 1; break;\
	case 4:	if(x > y) return 1; break;\
	case 5:	if(x >= y) return 1; break;\
	}\
	return 0;

#define	TRUTH(x)\
	switch(op){\
	case 0:	return !x;\
	case 1:	if(x) return 1; break;\
	case 2:	return x && j;\
	case 3:	return x || j;\
	case 4:	return x ? 2 : 3;\
	case 5:	if(!x) return 1; break;\
	}\
	return 0;

/* global vs cast, cast vs global, cast vs cast, cast vs constant; branch forms; truth */
#define	FORMS(T, g, K, fa, fb, fc, fd, fe, ff, fg, ft)\
int fa(int op, int i, int j) { USED(j); REL(g, (T)i) }\
int fb(int op, int i, int j) { USED(j); REL((T)i, g) }\
int fc(int op, int i, int j) { REL((T)i, (T)j) }\
int fd(int op, int i, int j) { USED(j); REL((T)i, K) }\
int fe(int op, int i, int j) { USED(j); BREL(g, (T)i) }\
int ff(int op, int i, int j) { BREL((T)i, (T)j) }\
int fg(int op, int i, int j) { USED(j); BREL(K, (T)i) }\
int ft(int op, int i, int j) { TRUTH((T)i) }

FORMS(char, gc, -128, ca, cb, cc, cd, ce, cf, cg, ct)
FORMS(uchar, guc, 0x80, uca, ucb, ucc, ucd, uce, ucf, ucg, uct)
FORMS(short, gs, -32768, sa, sb, sc, sd, se, sf, sg, st)
FORMS(ushort, gus, 0x8000, usa, usb, usc, usd, use, usf, usg, ust)

char
retc(int i)
{
	return i;
}

ushort
retus(int i)
{
	return i;
}

int
parc(char c, int j)
{
	return c == gc && c <= j;
}

int
parus(ushort u, int j)
{
	USED(j);
	return u == gus;
}

/* narrow values that are not casts */
int
misc(int op, int i, int j)
{
	char c;
	uchar uc;
	short s;
	int k;

	switch(op){
	case 0:	return (c = i) == gc;
	case 1:	return (gc = i) == (char)j;
	case 2:	c = gc; return (c += i) == (char)j;
	case 3:	c = gc; return ++c == (char)(j+i);
	case 4:	uc = guc; return (uc -= i) < (uchar)j;
	case 5:	s = gs; return (s *= i) == (short)j;
	case 6:	return retc(i) == gc;
	case 7:	return retus(i) < gus;
	case 8:	return parc(i, j);
	case 9:	return parus(i, j);
	case 10:	return (j&1 ? (char)i : gc) == gc;
	case 11:	return (uchar)(char)i == 0x80;
	case 12:	return (char)(short)i == gc;
	case 13:	return (short)(char)i == gs;
	case 14:	return (uchar)(short)i < guc;
	case 15:	return (char)(i>>1) == gc;
	case 16:	return (char)(i+j) == gc;
	case 17:	return (char)(i&j) < 0;
	case 18:	return (uchar)~i == guc;
	case 19:	return (char)-i == gc;
	case 20:
		switch((char)i){
		case -128:	return 1;
		case 0:	return 2;
		case -1:	return 3;
		}
		return 0;
	case 21:
		switch(c = i){
		case -128:	return 1;
		case 0:	return 2;
		}
		return 0;
	case 22:
		switch((ushort)i){
		case 0x8000:	return 1;
		case 0xffff:	return 2;
		}
		return 0;
	case 23:
		for(k = 0, c = i; c != gc && k < 300; c++)
			k++;
		return k;
	case 24:	c = i; if(c) return 1; return 0;
	case 25:	return (char)i == (uchar)j;
	case 26:	return (short)i < (ushort)j;
	case 27:	return (char)i < (short)j;
	}
	return -1;
}

/* 32-bit values narrowed from vlong */
int
vint(int op, int i, int j)
{
	vlong v, w;

	v = ((vlong)j<<32) | (uint)i;
	w = ((vlong)i<<32) | (uint)j;
	switch(op){
	case 0:	return (int)v == (int)w;
	case 1:	return (int)v < (int)w;
	case 2:	return (uint)v < (uint)w;
	case 3:	return (long)v <= gi;
	case 4:	return (ulong)v >= (ulong)gi;
	case 5:	return (int)v < 0;
	case 6:	return !(int)v;
	case 7:	if((uint)w) return 1; return 0;
	case 8:	return (int)(v>>16) == (int)(w>>16);
	case 9:	return (uint)(v>>16) > (uint)(w>>16);
	case 10:	return (int)v == i && (uint)w == j;
	case 11:	return (char)v == (char)w;
	case 12:	return (ushort)v < (ushort)w;
	}
	return -1;
}

typedef struct Test Test;
struct Test
{
	char	*name;
	int	(*f)(int, int, int);
	int	nop;
};

Test	tests[] = {
	"ca", ca, 6, "cb", cb, 6, "cc", cc, 6, "cd", cd, 6,
	"ce", ce, 6, "cf", cf, 6, "cg", cg, 6, "ct", ct, 6,
	"uca", uca, 6, "ucb", ucb, 6, "ucc", ucc, 6, "ucd", ucd, 6,
	"uce", uce, 6, "ucf", ucf, 6, "ucg", ucg, 6, "uct", uct, 6,
	"sa", sa, 6, "sb", sb, 6, "sc", sc, 6, "sd", sd, 6,
	"se", se, 6, "sf", sf, 6, "sg", sg, 6, "st", st, 6,
	"usa", usa, 6, "usb", usb, 6, "usc", usc, 6, "usd", usd, 6,
	"use", use, 6, "usf", usf, 6, "usg", usg, 6, "ust", ust, 6,
	"misc", misc, 28,
	"vint", vint, 13,
};

void
main(void)
{
	Test *t;
	int op, a, b, r;
	char buf[8*nelem(vals)*nelem(vals)], *s;

	for(t = tests; t < tests+nelem(tests); t++)
		for(op = 0; op < t->nop; op++){
			s = buf;
			for(a = 0; a < nelem(vals); a++)
				for(b = 0; b < nelem(vals); b++){
					gc = vals[b];
					guc = vals[b];
					gs = vals[b];
					gus = vals[b];
					gi = vals[b];
					r = (*t->f)(op, vals[a], vals[b]);
					if(r >= 0 && r < 10)
						*s++ = '0'+r;
					else
						s += sprint(s, "<%d>", r);
				}
			*s = 0;
			print("%s %d %s\n", t->name, op, buf);
		}
	exits(nil);
}
