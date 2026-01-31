# 9term
CC=gcc
WILY=../..
BINDIR=$(h)/obj/sun4d/wily
CFLAGS  = -g -O -I../.. -I../../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1 -lnsl  
#../../libmsg/libmsg.a  ../../libXg/libXg.a
LIBS=-L../../libmsg -lmsg -lnsl 

TARGETS=	Wgoto Wexec Wins 
OBJECTS=$(TARGETS:%=%.o)

$(TARGETS): *.c

Wins:	Wins.o
	gcc -o Wins Wins.o $(LIBS) 
Wexec:	Wexec.o
	gcc -o Wexec Wexec.o $(LIBS) 
Wgoto:	Wgoto.o
	gcc -o Wgoto Wgoto.o $(LIBS) 
clean:
	- rm $(OBJECTS)
nuke:
	- rm $(OBJECTS) $(TARGETS)
