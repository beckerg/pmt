# $Id: Makefile 394 2017-01-10 13:10:13Z greg $

KMOD    = pmt

SRCS    = pmt.c tests.c

.include <bsd.kmod.mk>

#CFLAGS	+= -DINVARIANTS
#CFLAGS	+= -DDIAGNOSTIC
#CFLAGS	+= -O0 -g

cscope tags:

objdump:
	objdump -sdwx -Mintel --prefix-addresses pmt.o > pmt.objdump
	objdump -sdwx -Mintel --prefix-addresses tests.o > tests.objdump
