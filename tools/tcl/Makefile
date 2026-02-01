#
#	$Header: /u/cvs/wtcl/Makefile,v 1.2 1996/11/12 23:33:55 cvs Exp $
#
CC=gcc
W=./../../
WINC=-I$W -I$W/include -I$W/libXg -I$W/libmsg

INC= -I/usr/include/tcl8.6/ $(WINC)
WLD=-L$W/libXg -L$W/libmsg -lmsg -lXg
CFLAGS=-Wall $(INC)
LDFLAGS= -ltk -ltcl -lX11 -lm $(WLD)
T=wtcl wtk
S=wtcl.c wtk.c wily.c
O=$(S:.c=.o)

all : $T

wtcl : wtcl.o wily.o
	$(CC) -o $@ wtcl.o wily.o $(LDFLAGS)

wtk : wtk.o wily.o
	$(CC) -o $@ wtk.o wily.o $(LDFLAGS)

clean :
	rm -f $T $O core
