#include <u.h>
#include <libc.h>

/* argcheck n x2 x3 ... x(n-1): verify argc and the argument strings */
void
main(int argc, char **argv)
{
	int i, n, bad;
	char buf[32];

	bad = 0;
	if(argc < 2 || argv[1] == nil){
		fprint(2, "argcheck: BAD argc %d argv[1] %p\n", argc, argc>1 ? argv[1] : nil);
		exits("bad");
	}
	n = atoi(argv[1]);
	if(n != argc){
		fprint(2, "argcheck: BAD argc %d want %d\n", argc, n);
		bad++;
	}
	for(i = 2; i < argc; i++){
		snprint(buf, sizeof buf, "x%d", i);
		if(argv[i] == nil){
			fprint(2, "argcheck: BAD argv[%d] nil (argc %d)\n", i, argc);
			bad++;
		}else if(strcmp(argv[i], buf) != 0){
			fprint(2, "argcheck: BAD argv[%d] %q want %s\n", i, argv[i], buf);
			bad++;
		}
	}
	if(argv[argc] != nil){
		fprint(2, "argcheck: BAD argv[argc] not nil\n");
		bad++;
	}
	if(bad)
		exits("bad");
	exits(nil);
}
