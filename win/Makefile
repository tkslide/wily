CC	= gcc
CFLAGS	=
prefix = /usr/local
exec_prefix = ${prefix}
bindir = $(exec_prefix)/bin
W       = ./..

all: win wgoto wreplace

win: win.o
	gcc -g -O -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  $(W)/libmsg/libmsg.a  $(W)/libXg/libXg.a

win.o: win.c
	$(CC) -c  -I$(W)/include -I$(W) -DGNU $<

wgoto: wgoto.o
	gcc -g -O -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  $(W)/libmsg/libmsg.a  $(W)/libXg/libXg.a

wgoto.o: wgoto.c
	$(CC) -c  -I$(W)/include -I$(W) -DGNU $<

wreplace: wreplace.o
	gcc -g -O -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  $(W)/libmsg/libmsg.a  $(W)/libXg/libXg.a

wreplace.o: wreplace.c
	$(CC) -c  -I$(W)/include -I$(W) -DGNU $<

clean:
	@rm -f *.o win wgoto wreplace

nuke: clean


install:
	@cp win $(bindir)
	@cp wgoto $(bindir)
	@cp wreplace $(bindir)
