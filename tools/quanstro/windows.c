#include <unistd.h>
#include <u.h>
#include <libc.h>
#include <libg.h>
#include <msg.h>
#include <signal.h>
#include <sys/limits.h>

volatile bool control_c = false;

void
e(const char *s)
{
	perror(s);
	exit(1);
}

void
catch(int sig){
	signal(sig, catch);
	control_c = true;
}

void
main(int c, char**v){
	int fd;
	Handle *h;
	bool status;
	char *p;

	signal(SIGHUP, catch);
	signal(SIGINT, catch);
	signal(SIGQUIT, catch);
	signal(SIGTERM, catch);

	fd = client_connect();
	if (-1 == fd){
		e("can't open wilyfifo");
	}
	h = rpc_init(fd);
	
	status = true;
	rpc_list(h, &p);
	printf("%s", p);
	free(p);
	rpc_freehandle(h);
	exit(!status);
}
