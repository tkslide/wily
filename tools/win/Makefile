CC	= gcc
CFLAGS	= 

all: win wgoto wreplace

win: win.o
	gcc -g -O -I.. -I./../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  ../../libmsg/libmsg.a  ../../libXg/libXg.a

		
win.o: win.c
	$(CC) -c  -I../../include -I../.. -DGNU $<

wgoto: wgoto.o
	gcc -g -O -I.. -I./../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  ../../libmsg/libmsg.a  ../../libXg/libXg.a
		
wgoto.o: wgoto.c
	$(CC) -c  -I../../include -I../.. -DGNU $<

wreplace: wreplace.o
	gcc -g -O -I.. -I./../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1   -o $@ $< -lnsl  ../../libmsg/libmsg.a  ../../libXg/libXg.a
		
wreplace.o: wreplace.c
	$(CC) -c  -I../../include -I../.. -DGNU $<

clean:
	@rm -f *.o win wgoto wreplace
