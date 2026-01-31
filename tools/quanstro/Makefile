W = ../..
CPPFLAGS = -I$W -I$W/include -I$W/libXg -I$W/include -I$W/sam/include
LDFLAGS = -L$W/libXg -L$W/libmsg
LDLIBS = -lmsg -lXg 
# gmake CFLAGS = -g -Wall -ansi
#CFLAGS = -g -Wall $(CPPFLAGS) $(LDLIBS)
CFLAGS  = -g -O -I../.. -I./../include -I./../sam/include -DHAVE_LIBNSL=1 -DHAVE_LIBSOCKET=1 -DHAVE_DEV_PTMX=1 -DHAVE_SYS_PTEM_H=1 -lnsl  ../../libmsg/libmsg.a  ../../libXg/libXg.a
CC=gcc
PROGS = wcat windows wreplace wattach wread

all: $(PROGS) 

install:
	cp $(PROGS) $(binpath)

clean:
	rm $(PROGS)
