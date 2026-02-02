# 9term
CC=gcc
WILY=../..
CFLAGS  = -g -O -I../.. -I../../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1 -lnsl
LIBS=-L../../libmsg -lmsg -lnsl

TARGETS=	Wgoto Wexec Wins
OBJECTS=$(TARGETS:%=%.o)

all: $(TARGETS)

Wins: Wins.o
	gcc -o $@ $< $(LIBS)
Wexec: Wexec.o
	gcc -o $@ $< $(LIBS)
Wgoto:	Wgoto.o
	gcc -o $@ $< $(LIBS)
clean:
	- rm -f $(OBJECTS)
nuke:
	- rm -f $(OBJECTS) $(TARGETS)
