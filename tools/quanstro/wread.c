#include <unistd.h>
#include <u.h>
#include <libc.h>
#include <libg.h>
#include <msg.h>
#include <signal.h>
#include <limits.h>

volatile bool control_c = false;

enum {
	BLOCK = 8 * 1024
};

void q_exits(const char*whine){
	if (*whine){
		fprintf(stderr, "%s\n", whine);
		exit(1);
	}
	exit(0);
}


void
catch(int sig){
	signal(sig, catch);
	control_c = true;
}

const char* read_buf(Handle *h, long id, long len){
	Range R;
	long p;
	char buf[UTFmax * BLOCK];
	const char* s;
	int delta;

	for(p = 0; p + BLOCK <= len; p += BLOCK){
		R.p0 = p;
		R.p1 = p + BLOCK-1;
		s = rpc_read(h, id, R, buf);
		if (s){
			return s;
		}
		delta=strlen(&buf[BLOCK-1]);
		if( write(1, buf, BLOCK+delta)); /* ignore return code */
	}
	R.p0 = p;
	R.p1 = len;
	s = rpc_read(h, id, R, buf);
	if (s){
		return s;
	}
	delta=strlen(&buf[len-p]);
	if (write(1, buf, R.p1 - R.p0 + delta));	/* wrong! */
	return 0;
}

void
main(int c, char**v){
	int fd;
	Handle *h;
	bool status;
	const char* s;
	Range R;
	long id;
	char* r;

	c--;
	v++;
	if (c != 1){
		q_exits("usage: wread <windowid>");
	}

	id = strtoul(*v, &r, 0);
	if (*r){
		q_exits("doesn't look integerish");
	}

	signal(SIGHUP, catch);
	signal(SIGINT, catch);
	signal(SIGQUIT, catch);
	signal(SIGTERM, catch);

	fd = client_connect();
	if (-1 == fd){
		q_exits("can't open wilyfifo");
	}
	h = rpc_init(fd);
	
	s=rpc_goto(h, (Id*)&id, &R, (char*)":$", false);
	if (s){
		q_exits(s);
	}

	read_buf(h, id, R.p1);

	rpc_freehandle(h);
	exit(!status);
}
